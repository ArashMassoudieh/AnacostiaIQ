#!/usr/bin/env bash
set -euo pipefail

# Deploy a separate lab dashboard under /lab/ by reusing the WebAssembly
# bundle already installed for the field dashboard. Only the lab-specific
# runtime configuration and shared health page are uploaded from the repo.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PEM="${DASH_PEM:-$HOME/.ssh/ArashLinux.pem}"
REMOTE_HOST="${DASH_HOST:-ec2-54-213-147-59.us-west-2.compute.amazonaws.com}"
REMOTE_USER="${DASH_USER:-ubuntu}"
REMOTE_ROOT="${DASH_DOCROOT:-/home/ubuntu/dashboard}"
SSH_PORT="${DASH_PORT:-22}"

while getopts "i:H:u:d:P:h" opt; do
  case "$opt" in
    i) PEM="$OPTARG" ;;
    H) REMOTE_HOST="$OPTARG" ;;
    u) REMOTE_USER="$OPTARG" ;;
    d) REMOTE_ROOT="$OPTARG" ;;
    P) SSH_PORT="$OPTARG" ;;
    h) sed -n '1,12p' "$0"; exit 0 ;;
    *) exit 2 ;;
  esac
done

[[ -f "$PEM" ]] || { echo "ERROR: missing SSH key: $PEM" >&2; exit 1; }
[[ -f "$SCRIPT_DIR/config.lab.json" ]] || { echo "ERROR: missing config.lab.json" >&2; exit 1; }
[[ -f "$SCRIPT_DIR/web/health.html" ]] || { echo "ERROR: missing web/health.html" >&2; exit 1; }

chmod 600 "$PEM"
TARGET="$REMOTE_USER@$REMOTE_HOST"
SSH_OPTS=(-i "$PEM" -p "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=10)
SCP_OPTS=(-i "$PEM" -P "$SSH_PORT" -o BatchMode=yes -o ConnectTimeout=10)
LAB_ROOT="$REMOTE_ROOT/lab"
STAGING="/tmp/lab-dashboard-upload-$(date +%Y%m%d-%H%M%S)"

ssh "${SSH_OPTS[@]}" "$TARGET" "
  set -e
  mkdir -p '$STAGING'
  for f in SensorDashboard.html SensorDashboard.js SensorDashboard.wasm qtloader.js; do
    test -f '$REMOTE_ROOT/'\"\$f\"
    cp '$REMOTE_ROOT/'\"\$f\" '$STAGING/'
  done
  test ! -f '$REMOTE_ROOT/qtlogo.svg' || cp '$REMOTE_ROOT/qtlogo.svg' '$STAGING/'
  cp '$REMOTE_ROOT/SensorDashboard.html' '$STAGING/index.html'
"

scp "${SCP_OPTS[@]}" -q "$SCRIPT_DIR/config.lab.json" "$TARGET:$STAGING/config.json"
scp "${SCP_OPTS[@]}" -q "$SCRIPT_DIR/web/health.html" "$TARGET:$STAGING/health.html"

ssh "${SSH_OPTS[@]}" "$TARGET" "
  set -e
  mkdir -p '$LAB_ROOT'
  cp -f '$STAGING'/* '$LAB_ROOT/'
  chmod 755 '$LAB_ROOT'
  find '$LAB_ROOT' -maxdepth 1 -type f -exec chmod 644 {} +
  rm -rf '$STAGING'
"

BASE="http://$REMOTE_HOST/lab"
for path in / /config.json /health.html /SensorDashboard.wasm; do
  code="$(curl -sS -o /dev/null -w '%{http_code}' "$BASE$path")"
  [[ "$code" == 200 ]] || { echo "ERROR: $BASE$path returned $code" >&2; exit 1; }
done

echo "Lab dashboard deployed: $BASE/"
echo "Lab health: $BASE/health.html"
