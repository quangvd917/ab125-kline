#!/usr/bin/env bash
# Build and flash one of the firmwares, picking the board by its MAC address so the
# K-Line reader and the CYD dashboard never get each other's firmware.
#
#   ./flash.sh -e c3_supermini      K-Line reader on ESP32-C3 SuperMini + L9637D
#   ./flash.sh -e wrover_2n2222     K-Line reader on ESP32 + 2N2222 (v1 circuit)
#   ./flash.sh -e cyd               CYD 2.8" dashboard
# Options:
#   -m        open the serial monitor after flashing
#   -p PORT   use this port (the MAC is still checked)
#   -f        flash even without a known MAC (only one board connected)
#   -b        build only
#
# Board MACs live in firmware/boards.local (git-ignored), one "env=mac" per line;
# see boards.local.example. BOARD_MAC=xx:xx:... overrides it for one run.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ENV="" PORT="" MONITOR=0 FORCE=0 BUILD_ONLY=0
while getopts "e:p:mfbh" opt; do
  case $opt in
    e) ENV="$OPTARG" ;;
    p) PORT="$OPTARG" ;;
    m) MONITOR=1 ;;
    f) FORCE=1 ;;
    b) BUILD_ONLY=1 ;;
    *) sed -n '2,16p' "$0"; exit 0 ;;
  esac
done

case "$ENV" in
  c3_supermini | wrover_2n2222) PROJECT_DIR="$HERE/kline-reader" ;;
  cyd) PROJECT_DIR="$HERE/dashboard-cyd" ;;
  *) sed -n '2,16p' "$0"; exit 1 ;;
esac

PIO="$(command -v pio || true)"
[[ -z "$PIO" ]] && PIO="$HOME/.platformio/penv/bin/pio"
PY="$HOME/.platformio/penv/bin/python"
ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
[[ -x "$PIO" ]] || { echo "PlatformIO not found (install it or the VS Code extension)"; exit 1; }

if [[ $BUILD_ONLY -eq 1 ]]; then
  exec "$PIO" run -d "$PROJECT_DIR" -e "$ENV"
fi

BOARDS="$HERE/boards.local"
mac_for_env() { [[ -f "$BOARDS" ]] && awk -F= -v e="$1" '$1==e {print tolower($2)}' "$BOARDS" | tr -d ' \r'; }
env_for_mac() { [[ -f "$BOARDS" && -n "$1" ]] && awk -F= -v m="$1" 'tolower($2)==m {print $1}' "$BOARDS" | tr -d ' \r'; }

EXPECTED_MAC="$(echo "${BOARD_MAC:-$(mac_for_env "$ENV")}" | tr 'A-F' 'a-f')"

read_mac() {
  "$PY" "$ESPTOOL" --port "$1" read_mac 2>/dev/null | awk '/^MAC:/ {print tolower($2); exit}'
}

if [[ -n "$PORT" ]]; then
  CANDIDATES=("$PORT")
else
  CANDIDATES=()
  for p in /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* /dev/ttyUSB* /dev/ttyACM*; do
    [[ -e "$p" ]] && CANDIDATES+=("$p")
  done
  [[ ${#CANDIDATES[@]} -eq 0 ]] && { echo "No ESP32 serial port found. Is the board plugged in?"; exit 1; }
fi

TARGET=""
for p in "${CANDIDATES[@]}"; do
  mac="$(read_mac "$p")"
  label="$(env_for_mac "$mac")"
  [[ -z "$label" ]] && label="unknown board"
  [[ -z "$mac" ]] && label="no answer"
  echo "  $p  ${mac:-?}  ($label)"
  [[ -n "$EXPECTED_MAC" && "$mac" == "$EXPECTED_MAC" ]] && TARGET="$p"
done

if [[ -z "$TARGET" ]]; then
  if [[ $FORCE -eq 1 && ${#CANDIDATES[@]} -eq 1 ]]; then
    TARGET="${CANDIDATES[0]}"
    echo "No known MAC for '$ENV', flashing the only connected board (-f)."
    echo "Tip: add '$ENV=$(read_mac "$TARGET")' to $BOARDS to skip -f next time."
  elif [[ -z "$EXPECTED_MAC" ]]; then
    echo "No MAC for '$ENV' in $BOARDS."
    echo "Add a line '$ENV=<mac shown above>', or use -f with only this board connected."
    exit 1
  else
    echo "No board with MAC $EXPECTED_MAC ($ENV) is connected."
    exit 1
  fi
fi

echo "Flashing $ENV -> $TARGET"
"$PIO" run -d "$PROJECT_DIR" -e "$ENV" -t upload --upload-port "$TARGET"

if [[ $MONITOR -eq 1 ]]; then
  sleep 2  # native-USB boards (C3) re-enumerate after reset
  exec "$PIO" device monitor -p "$TARGET" -b 115200
fi
