#!/usr/bin/env bash
# Flash the K-Line reader. Default board: ESP32-C3 SuperMini + L9637D.
# Pass -e wrover_2n2222 for the v1 circuit; see ../flash.sh for all options.
HERE="$(cd "$(dirname "$0")" && pwd)"
[[ " $* " == *" -e "* ]] && exec "$HERE/../flash.sh" "$@"
exec "$HERE/../flash.sh" -e c3_supermini "$@"
