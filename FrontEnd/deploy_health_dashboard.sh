#!/usr/bin/env bash
set -euo pipefail

PEM="${DASH_PEM:-./ArashLinux.pem}"
REMOTE_HOST="${DASH_HOST:-ec2-54-213-147-59.us-west-2.compute.amazonaws.com}"
REMOTE_USER="${DASH_USER:-ubuntu}"
DOCROOT="${DASH_DOCROOT:-/home/ubuntu/dashboard}"
SSH_PORT="${DASH_PORT:-22}"
SRC="${DASH_HEALTH_SRC:-./web/health.html}"

usage(){
  echo "Usage: $0 [-i key.pem] [-H host] [-u user] [-d docroot] [-P port] [-s health.html]"; exit "${1:-0}";
}
while getopts "i:H:u:d:P:s:h" opt; do
  case "$opt" in
    i) PEM="$OPTARG";; H) REMOTE_HOST="$OPTARG";; u) REMOTE_USER="$OPTARG";; d) DOCROOT="$OPTARG";; P) SSH_PORT="$OPTARG";; s) SRC="$OPTARG";; h) usage 0;; *) usage 1;;
  esac
done

[[ -f "$SRC" ]] || { echo "ERROR: missing $SRC" >&2; exit 1; }
[[ -f "$PEM" ]] || { echo "ERROR: missing PEM $PEM" >&2; exit 1; }
chmod 600 "$PEM"
TARGET="$REMOTE_USER@$REMOTE_HOST"
ssh -i "$PEM" -p "$SSH_PORT" "$TARGET" "mkdir -p '$DOCROOT'"
scp -i "$PEM" -P "$SSH_PORT" "$SRC" "$TARGET:$DOCROOT/health.html"
ssh -i "$PEM" -p "$SSH_PORT" "$TARGET" "chmod 644 '$DOCROOT/health.html'"

BASE="http://$REMOTE_HOST"
code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/health.html" || true)
if [[ "$code" != "200" ]]; then
  echo "ERROR: $BASE/health.html returned HTTP $code" >&2
  exit 1
fi

echo "Health portal deployed: $BASE/health.html"
