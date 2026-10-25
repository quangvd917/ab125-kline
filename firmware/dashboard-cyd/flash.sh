#!/usr/bin/env bash
# Flash the CYD dashboard; see ../flash.sh for all options.
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/../flash.sh" -e cyd "$@"
