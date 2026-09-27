# Driver do melonDS para testes do DSi Dash.
#   emu.ps1 start                      -> (re)abre o melonDS com dsidash.nds
#   emu.ps1 shot nome                  -> salva %TEMP%\nome.png (area do cliente)
#   emu.ps1 key A|B|X|Y|L|R|START|SELECT|UP|DOWN|LEFT|RIGHT [n] [holdms]
#   emu.ps1 tap x y                    -> toque na tela de baixo (coords DS 0..255, 0..191)
#   emu.ps1 drag x1 y1 x2 y2 [passos]
#   emu.ps1 stop
param([string]$cmd, [string]$a1, [string]$a2, [string]$a3, [string]$a4, [string]$a5)

Add-Type -AssemblyName System.Drawing
if (-not ("EmuW" -as [type])) {
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class EmuW {
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern IntPtr PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  public struct RECT { public int L, T, R, B; }
  public struct POINT { public int X, Y; }
}
'@
}

$root = Split-Path -Parent $PSScriptRoot
$emu = Join-Path (Split-Path -Parent $root) "tools\melonDS.exe"
$rom = Join-Path $root "dsidash.nds"

function Get-Win {
  $p = Get-Process melonDS -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if (-not $p) { throw "melonDS nao esta aberto" }
  return $p.MainWindowHandle
}

# retangulo da tela de baixo em coordenadas do cliente
function Get-BottomRect($h) {
  $r = New-Object EmuW+RECT
  [EmuW]::GetClientRect($h, [ref]$r) | Out-Null
  $menu = 21
  $pw = $r.R; $ph = $r.B - $menu
  $s = [Math]::Min($pw / 256.0, $ph / 384.0)
  $ox = ($pw - 256 * $s) / 2
  $oy = $menu + ($ph - 384 * $s) / 2 + 192 * $s
  return @{ X = $ox; Y = $oy; S = $s }
}

function Send-Key($h, $name, $holdMs) {
  $map = @{ A = 0x58; B = 0x5A; X = 0x53; Y = 0x41; L = 0x51; R = 0x57; START = 0x0D; SELECT = 0x08;
            UP = 0x26; DOWN = 0x28; LEFT = 0x25; RIGHT = 0x27 }
  $vk = $map[$name.ToUpper()]
  if (-not $vk) { throw "tecla desconhecida: $name" }
  $sc = [EmuW]::MapVirtualKey($vk, 0)
  $ext = 0
  if ($vk -ge 0x25 -and $vk -le 0x28) { $ext = 1 -shl 24 }
  $down = [IntPtr](1 -bor ($sc -shl 16) -bor $ext)
  $up = [IntPtr]([int64](1 -bor ($sc -shl 16) -bor $ext -bor (1 -shl 30)) -bor ([int64]1 -shl 31))
  [EmuW]::PostMessage($h, 0x100, [IntPtr]$vk, $down) | Out-Null
  Start-Sleep -Milliseconds $holdMs
  [EmuW]::PostMessage($h, 0x101, [IntPtr]$vk, $up) | Out-Null
}

function Mouse($h, $msg, $cx, $cy, $btn) {
  $l = [IntPtr](([int]$cy -shl 16) -bor ([int]$cx -band 0xFFFF))
  [EmuW]::PostMessage($h, $msg, [IntPtr]$btn, $l) | Out-Null
}

function DsToClient($b, $x, $y) {
  return @([int]($b.X + ($x + 0.5) * $b.S), [int]($b.Y + ($y + 0.5) * $b.S))
}

switch ($cmd) {
  "start" {
    Get-Process melonDS -ErrorAction SilentlyContinue | ForEach-Object { $_.CloseMainWindow() | Out-Null }
    Start-Sleep -Milliseconds 1500
    Get-Process melonDS -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Process -FilePath $emu -ArgumentList "`"$rom`"" -WorkingDirectory (Split-Path $emu) | Out-Null
    $wait = 12; if ($a1) { $wait = [int]$a1 }
    Start-Sleep -Seconds $wait
    "started"
  }
  "stop" {
    Get-Process melonDS -ErrorAction SilentlyContinue | ForEach-Object { $_.CloseMainWindow() | Out-Null }
    "stopped"
  }
  "shot" {
    $h = Get-Win
    $r = New-Object EmuW+RECT
    [EmuW]::GetClientRect($h, [ref]$r) | Out-Null
    $bmp = New-Object System.Drawing.Bitmap $r.R, $r.B
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [EmuW]::PrintWindow($h, $hdc, 3) | Out-Null
    $g.ReleaseHdc($hdc)
    # recorta so as telas (sem menu), escala 1:1 aproximada
    $b = Get-BottomRect $h
    $x0 = [int]$b.X; $y0 = [int]($b.Y - 192 * $b.S); $w = [int](256 * $b.S); $hh = [int](384 * $b.S)
    $crop = $bmp.Clone((New-Object System.Drawing.Rectangle $x0, $y0, $w, $hh), $bmp.PixelFormat)
    $out = New-Object System.Drawing.Bitmap 256, 384
    $g2 = [System.Drawing.Graphics]::FromImage($out)
    $g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g2.DrawImage($crop, 0, 0, 256, 384)
    $name = "shot"; if ($a1) { $name = $a1 }
    $path = Join-Path $env:TEMP "$name.png"
    if ($a2 -eq "top") {
      # tela de cima ampliada 2x (sem reamostragem suave), para ver detalhes
      $big = New-Object System.Drawing.Bitmap 512, 384
      $g3 = [System.Drawing.Graphics]::FromImage($big)
      $g3.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
      $g3.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
      $src = New-Object System.Drawing.Rectangle -ArgumentList $x0, $y0, $w, ([int]($hh / 2))
      $g3.DrawImage($bmp, (New-Object System.Drawing.Rectangle -ArgumentList 0, 0, 512, 384), $src, [System.Drawing.GraphicsUnit]::Pixel)
      $big.Save($path)
    } else {
      $out.Save($path)
    }
    $path
  }
  "key" {
    $h = Get-Win
    $n = 1; if ($a2) { $n = [int]$a2 }
    $hold = 90; if ($a3) { $hold = [int]$a3 }
    for ($i = 0; $i -lt $n; $i++) { Send-Key $h $a1 $hold; Start-Sleep -Milliseconds 120 }
    "ok"
  }
  "down" {
    # segura uma tecla (solte com "up")
    $h = Get-Win
    $map = @{ A = 0x58; B = 0x5A; X = 0x53; Y = 0x41; L = 0x51; R = 0x57; START = 0x0D; SELECT = 0x08; UP = 0x26; DOWN = 0x28; LEFT = 0x25; RIGHT = 0x27 }
    $vk = $map[$a1.ToUpper()]; $sc = [EmuW]::MapVirtualKey($vk, 0); $ext = 0
    if ($vk -ge 0x25 -and $vk -le 0x28) { $ext = 1 -shl 24 }
    [EmuW]::PostMessage($h, 0x100, [IntPtr]$vk, [IntPtr](1 -bor ($sc -shl 16) -bor $ext)) | Out-Null
    "ok"
  }
  "up" {
    $h = Get-Win
    $map = @{ A = 0x58; B = 0x5A; X = 0x53; Y = 0x41; L = 0x51; R = 0x57; START = 0x0D; SELECT = 0x08; UP = 0x26; DOWN = 0x28; LEFT = 0x25; RIGHT = 0x27 }
    $vk = $map[$a1.ToUpper()]; $sc = [EmuW]::MapVirtualKey($vk, 0); $ext = 0
    if ($vk -ge 0x25 -and $vk -le 0x28) { $ext = 1 -shl 24 }
    [EmuW]::PostMessage($h, 0x101, [IntPtr]$vk, [IntPtr]([int64](1 -bor ($sc -shl 16) -bor $ext -bor (1 -shl 30)) -bor ([int64]1 -shl 31))) | Out-Null
    "ok"
  }
  "tap" {
    $h = Get-Win
    $b = Get-BottomRect $h
    $c = DsToClient $b ([int]$a1) ([int]$a2)
    Mouse $h 0x200 $c[0] $c[1] 0
    Mouse $h 0x201 $c[0] $c[1] 1
    Start-Sleep -Milliseconds 120
    Mouse $h 0x202 $c[0] $c[1] 0
    "ok"
  }
  "drag" {
    $h = Get-Win
    $b = Get-BottomRect $h
    $steps = 12; if ($a5) { $steps = [int]$a5 }
    $x1 = [int]$a1; $y1 = [int]$a2; $x2 = [int]$a3; $y2 = [int]$a4
    $c = DsToClient $b $x1 $y1
    Mouse $h 0x201 $c[0] $c[1] 1
    for ($i = 1; $i -le $steps; $i++) {
      Start-Sleep -Milliseconds 30
      $c = DsToClient $b ($x1 + ($x2 - $x1) * $i / $steps) ($y1 + ($y2 - $y1) * $i / $steps)
      Mouse $h 0x200 $c[0] $c[1] 1
    }
    Start-Sleep -Milliseconds 60
    Mouse $h 0x202 $c[0] $c[1] 0
    "ok"
  }
  default { "comando desconhecido: $cmd" }
}
