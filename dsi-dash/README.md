# DSi Dash v2.1

Launcher para Nintendo DSi no estilo da tela inicial do Nintendo Switch, com apps
que usam o Wi-Fi em 2026: navegador com HTTPS (TLS 1.2) e imagens, notícias (RSS),
Wikipédia, mapas (OpenStreetMap), tradutor, clima, transferência de arquivos pelo
navegador do PC/celular, câmera (fotos JPEG + leitor de QR code), álbum, notas,
calculadora, gerenciador de arquivos e um jogo de corrida 3D (DSi Dash Racing).

Instruções de uso: [`LEIA-ME.txt`](LEIA-ME.txt) (também vai na raiz do pacote do cartão SD).

## Compilar

Git Bash, com o devkitPro do msys64 do usuário (`pacman -S nds-dev`):

```sh
./build.sh            # compila -> dsidash.nds
./build.sh assets     # regenera fontes/ícones (Python + Pillow) e compila
./build.sh clean      # limpa e compila tudo (BearSSL leva ~3 min)
python tools/gen_ta.py  # regenera as CAs (assets/cacert.pem, de https://curl.se/ca/cacert.pem)
python tools/gen_race.py  # regenera pista, carro e texturas da corrida (race_data.h, race_tex.bin)
```

Testar no emulador: `powershell -File tools/emu.ps1 start` (melonDS em `../tools/`,
com SD virtual sincronizado com `../tools/sdroot`). O mesmo script tira capturas
(`shot`), aperta botões (`key A`) e toca na tela (`tap x y`, `drag`).

## Estrutura

```
arm7/source/main.c     serviços padrão do calico + servidor PXI (brilho, reboot via Unlaunch, desligar, câmera)
arm7/source/camera.c   I2C das câmeras Aptina MT9V113 (portado do libnds do BlocksDS)
common/ipc.h           protocolo ARM9 <-> ARM7
arm9/source/
  main.c               laço principal, troca de apps com fade
  gfx.c                render por software RGB555 (retângulos arredondados AA, gradientes, sombras, ícones, texto UTF-8)
  ui.c, kbd.c          tema claro/escuro, entrada, barra de status, dicas de botão, listas, diálogos, teclado
  sys.c                relógio pela rede, configurações (INI no SD), bateria, brilho, sons, entropia, reboot
  net.c, net_tls.c     Wi-Fi, thread de rede, cliente HTTP (redirects, chunked), HTTPS com BearSSL
  html.c               parser HTML + layout em linhas + desenho (navegador, páginas internas)
  srv.c                servidor HTTP de arquivos (upload multipart em streaming)
  camera.c, jpeg.c     captura (NDMA) e codificador JPEG 4:2:2 em inteiros
  image.c              decodificação de imagens (stb_image)
  wx.c, json.c         dados de clima
  app_*.c              apps
  race_world.c         corrida: render 3D (videoGL), céu, terreno, pista com LOD, objetos, carros iluminados
  race_game.c          corrida: física, drift/mini-turbo, IA, voltas, colisões, câmera
  race_audio.c         corrida: motor e efeitos nos canais PSG
  app_race.c           corrida: menus, HUD sobre o 3D e painel da tela de toque
arm9/bearssl           BearSSL (TLS)
arm9/libs              quirc (QR), qrcodegen, stb_image
tools/gen_assets.py    gera fontes (Nunito) e ícones
tools/gen_ta.py        converte as CAs da Mozilla para o BearSSL
tools/gen_race.py      gera o Circuito Maceió (spline, linha ideal, velocidades), o carro e as texturas
```

A tela de baixo (toque) usa o engine principal com `lcdMainOnBottom()`. As duas telas
são desenhadas em buffers na RAM e copiadas por DMA logo após o VBlank, só quando mudam.
O trabalho de rede roda numa thread do calico com prioridade menor que a da interface.

Na corrida, o hardware 3D desenha na tela de cima (engine principal, BG0) e o HUD é um
bitmap 16 bits por cima (VRAM D); o painel da tela de toque é desenhado direto na VRAM,
restaurando só as áreas que mudam. O mundo fica em metros e os vértices são enviados
relativos à câmera (÷64, v16 cobre ±512 m). Como o ARM9 não tem FPU, o render usa só
inteiros (tabelas de seno/cosseno, listas por tipo, pista distante de 2 em 2 seções):
~60 qps no melonDS em modo DS (67 MHz), com antialias de bordas, neblina e luz especular.

## Créditos e licenças de terceiros

- BearSSL — MIT — Thomas Pornin (`arm9/bearssl/LICENSE.txt`)
- stb_image — domínio público / MIT — Sean Barrett
- quirc — ISC — Daniel Beer (`arm9/libs/quirc/LICENSE`)
- QR Code generator — MIT — Project Nayuki
- Driver das câmeras — zlib — libnds do BlocksDS (Adrian "asie" Siekierka e outros)
- Fonte Nunito — SIL Open Font License 1.1 (`assets/OFL-Nunito.txt`)
- Autoridades certificadoras — pacote da Mozilla (via curl.se), MPL 2.0
- devkitARM, libnds, calico e dswifi — devkitPro
- Dados: open-meteo.com, ip-api.com, Wikipedia, OpenStreetMap (tiles e Nominatim, ODbL), MyMemory, feeds RSS dos respectivos sites
