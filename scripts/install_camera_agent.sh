#!/usr/bin/env bash
set -euo pipefail

REPO="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TOKEN_FILE=""
API_URL="http://54.213.147.59:5000"
STATION_ID="lab_station_01"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --token-file) TOKEN_FILE="$2"; shift 2 ;;
    --api-url) API_URL="$2"; shift 2 ;;
    --station-id) STATION_ID="$2"; shift 2 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done

[[ -n "$TOKEN_FILE" && -f "$TOKEN_FILE" ]] || {
  echo "Usage: $0 --token-file PATH [--api-url URL] [--station-id ID]" >&2
  exit 2
}

CONFIG_DIR="$HOME/.config/anacostiaiq"
UNIT_DIR="$HOME/.config/systemd/user"
CONFIG="$CONFIG_DIR/camera.json"
UNIT="$UNIT_DIR/anacostiaiq-camera.service"
mkdir -p "$CONFIG_DIR" "$UNIT_DIR"

python3 - "$TOKEN_FILE" "$CONFIG" "$API_URL" "$STATION_ID" <<'PY'
import json, os, sys
token_path, config_path, api_url, station_id = sys.argv[1:]
with open(token_path, encoding="utf-8") as fh:
    token = fh.read().strip()
if not token:
    raise SystemExit("camera token file is empty")
with open(config_path, "w", encoding="utf-8") as fh:
    json.dump({
        "api_url": api_url,
        "station_id": station_id,
        "token": token,
        "poll_seconds": 15,
        "width": 1920,
        "height": 1080,
        "state_dir": "~/.local/state/anacostiaiq-camera",
    }, fh, indent=2)
    fh.write("\n")
os.chmod(config_path, 0o600)
PY

sed \
  -e "s|@PYTHON@|$(command -v python3)|g" \
  -e "s|@REPO@|$REPO|g" \
  -e "s|@CONFIG@|$CONFIG|g" \
  "$REPO/deploy/systemd/anacostiaiq-camera.service.in" > "$UNIT"

systemctl --user daemon-reload
systemctl --user enable --now anacostiaiq-camera.service
systemctl --user --no-pager status anacostiaiq-camera.service
