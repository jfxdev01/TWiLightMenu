#!/bin/sh
# Compila o DSi Dash a partir do Git Bash (usa o msys64 + devkitPro do usuario).
# Uso: ./build.sh [clean] [assets]
set -e
MAKE=/c/Users/Jota/msys64/usr/bin/make.exe
DKP=/c/Users/Jota/msys64/opt/devkitpro
W='C:/Users/Jota/AppData/Local/Temp'
export PATH=/c/Users/Jota/msys64/usr/bin:$DKP/devkitARM/bin:$DKP/tools/bin:$PATH
export TMP="$W" TEMP="$W" TMPDIR="$W"
cd "$(dirname "$0")"
for a in "$@"; do
	case "$a" in
		clean) "$MAKE" DEVKITPRO="$DKP" DEVKITARM="$DKP/devkitARM" clean ;;
		assets) /c/Python314/python -W ignore tools/gen_assets.py ;;
	esac
done
"$MAKE" DEVKITPRO="$DKP" DEVKITARM="$DKP/devkitARM" TMP="$W" TEMP="$W" TMPDIR="$W"
ls -la dsidash.nds
