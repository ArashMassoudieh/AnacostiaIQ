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

echo "==> Deploying AnacostiaIQ health portal"
echo "    source : $SRC"
echo "    target : $TARGET:$DOCROOT/health.html"

ssh "${SSH_OPTS[@]}" "$TARGET" "mkdir -p '$DOCROOT'"
scp "${SCP_OPTS[@]}" -q "$SRC" "$TARGET:$DOCROOT/health.html"
ssh "${SSH_OPTS[@]}" "$TARGET" "chmod 644 '$DOCROOT/health.html'"

BASE="http://$REMOTE_HOST"
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/health.html" || true)
if [[ "$code" != "200" ]]; then
  echo "ERROR: $BASE/health.html returned HTTP $code" >&2
  exit 1
fi

echo "OK: Health portal deployed"
echo "$BASE/health.html"
