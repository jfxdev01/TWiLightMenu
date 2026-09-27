#!/bin/sh
# Renders a screen of the Hub: ./shoot.sh <app index> <name> [extra script] [lang]
cd "$(dirname "$0")"
mkdir -p "sim:/_nds/TWiLightMenu/hub"
printf "[HUB]\nLANGUAGE = %s\nLAST_APP = %s\nDARK_THEME = ${DARK:-1}\n[WEATHER]\nCITY = São Paulo\n" "${4:-pt}" "$1" > "sim:/_nds/TWiLightMenu/hub/hub.ini"
HUB_SIM_SCRIPT="wait 20; key A; wait 4; $3; shot $2; quit" ./hubsim | grep saved
