#!/usr/bin/env bash
set -euo pipefail

# Deploy only the static health page. Designed to be runnable from any
# directory on a field/lab Pi after the repository has been cloned.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

REMOTE_HOST="${DASH_HOST:-ec2-54-213-147-59.us-west-2.compute.amazonaws.com}"
REMOTE_USER="${DASH_USER:-ubuntu}"
DOCROOT="${DASH_DOCROOT:-/home/ubuntu/dashboard}"
SSH_PORT="${DASH_PORT:-22}"
SRC="${DASH_HEALTH_SRC:-$SCRIPT_DIR/web/health.html}"
INDEX_SRC="${DASH_INDEX_SRC:-$SCRIPT_DIR/web/index.html}"
CONFIG_SRC="${DASH_CONFIG_SRC:-$SCRIPT_DIR/config.json}"
PEM="${DASH_PEM:-}"

usage(){
  cat <<EOF
Usage: $0 [-i key.pem] [-H host] [-u user] [-d docroot] [-P port] [-s health.html]

With no -i option, the script automatically checks:
  ~/ArashLinux.pem
  ~/.ssh/ArashLinux.pem
  $SCRIPT_DIR/ArashLinux.pem

Environment overrides: DASH_PEM, DASH_HOST, DASH_USER, DASH_DOCROOT,
DASH_PORT, DASH_HEALTH_SRC.
EOF
  exit "${1:-0}"
}

while getopts "i:H:u:d:P:s:h" opt; do
  case "$opt" in
    i) PEM="$OPTARG";;
    H) REMOTE_HOST="$OPTARG";;
    u) REMOTE_USER="$OPTARG";;
    d) DOCROOT="$OPTARG";;
    P) SSH_PORT="$OPTARG";;
    s) SRC="$OPTARG";;
    h) usage 0;;
    *) usage 1;;
  esac
done

[[ -f "$SRC" ]] || { echo "ERROR: missing health page: $SRC" >&2; exit 1; }
[[ -f "$INDEX_SRC" ]] || { echo "ERROR: missing landing page: $INDEX_SRC" >&2; exit 1; }

if [[ -z "$PEM" ]]; then
  for candidate in \
    "$HOME/ArashLinux.pem" \
    "$HOME/.ssh/ArashLinux.pem" \
    "$SCRIPT_DIR/ArashLinux.pem"; do
    if [[ -f "$candidate" ]]; then
      PEM="$candidate"
      break
    fi
  done
fi

if [[ -z "$PEM" || ! -f "$PEM" ]]; then
  echo "ERROR: ArashLinux.pem was not found automatically." >&2
  echo "       Put it at ~/ArashLinux.pem or ~/.ssh/ArashLinux.pem," >&2
  echo "       or pass -i /path/to/ArashLinux.pem." >&2
  exit 1
fi

chmod 600 "$PEM"
TARGET="$REMOTE_USER@$REMOTE_HOST"
SSH_OPTS=(-i "$PEM" -p "$SSH_PORT" -o ConnectTimeout=10)
SCP_OPTS=(-i "$PEM" -P "$SSH_PORT" -o ConnectTimeout=10)

echo "==> Deploying AnacostiaIQ public pages"
echo "    health : $SRC"
echo "    index  : $INDEX_SRC"
echo "    target : $TARGET:$DOCROOT"

ssh "${SSH_OPTS[@]}" "$TARGET" "mkdir -p '$DOCROOT'"
scp "${SCP_OPTS[@]}" -q "$SRC" "$TARGET:$DOCROOT/health.html"
scp "${SCP_OPTS[@]}" -q "$INDEX_SRC" "$TARGET:$DOCROOT/index.html"
scp "${SCP_OPTS[@]}" -q "$CONFIG_SRC" "$TARGET:$DOCROOT/config.json"
ssh "${SSH_OPTS[@]}" "$TARGET" "chmod 644 '$DOCROOT/health.html' '$DOCROOT/index.html' '$DOCROOT/config.json'"

BASE="http://$REMOTE_HOST"
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/health.html" || true)
if [[ "$code" != "200" ]]; then
  echo "ERROR: $BASE/health.html returned HTTP $code" >&2
  exit 1
fi

echo "OK: AnacostiaIQ public pages deployed"
echo "$BASE/health.html"
