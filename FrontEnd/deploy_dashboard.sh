#!/usr/bin/env bash
#
# deploy_dashboard.sh — upload the Qt WebAssembly dashboard build to the
# EC2 Apache docroot. Apache is assumed already configured (vhost +
# headers); this only ships files, fixes permissions, and verifies.
#
# Usage:
#   ./deploy_dashboard.sh
#   ./deploy_dashboard.sh -i ~/keys/ArashLinux.pem
#   ./deploy_dashboard.sh -b /path/to/WebAssembly_..._Release -H 1.2.3.4
#
# Config precedence: CLI flag > env var > built-in default.

set -euo pipefail

# ── Defaults (override via env or flags) ───────────────────────────
PEM="${DASH_PEM:-./ArashLinux.pem}"
REMOTE_HOST="${DASH_HOST:-ec2-54-213-147-59.us-west-2.compute.amazonaws.com}"
REMOTE_USER="${DASH_USER:-ubuntu}"
DOCROOT="${DASH_DOCROOT:-/home/ubuntu/dashboard}"
BUILD_DIR="${DASH_BUILD:-./build/WebAssembly_Qt_6_8_2_single_threaded-Release}"
SSH_PORT="${DASH_PORT:-22}"

# Files the dashboard needs at runtime. qtlogo.svg is referenced by the
# generated loader page; config.json drives the sensor selection.
REQUIRED_FILES=(
    "SensorDashboard.html"
    "SensorDashboard.js"
    "SensorDashboard.wasm"
    "qtloader.js"
    "config.json"
)
OPTIONAL_FILES=(
    "qtlogo.svg"
)

# ── Parse flags ────────────────────────────────────────────────────
usage() {
    sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while getopts "i:H:u:d:b:P:h" opt; do
    case "$opt" in
        i) PEM="$OPTARG" ;;
        H) REMOTE_HOST="$OPTARG" ;;
        u) REMOTE_USER="$OPTARG" ;;
        d) DOCROOT="$OPTARG" ;;
        b) BUILD_DIR="$OPTARG" ;;
        P) SSH_PORT="$OPTARG" ;;
        h) usage 0 ;;
        *) usage 1 ;;
    esac
done

TARGET="${REMOTE_USER}@${REMOTE_HOST}"
SSH_OPTS=(-i "$PEM" -p "$SSH_PORT")
SCP_OPTS=(-i "$PEM" -P "$SSH_PORT")   # note: scp uses -P (capital) for port

# ── Pre-flight checks (local) ──────────────────────────────────────
echo "==> Pre-flight checks"

if [[ ! -f "$PEM" ]]; then
    echo "ERROR: PEM key not found: $PEM" >&2
    echo "       Pass it with -i /path/to/key.pem or set DASH_PEM." >&2
    exit 1
fi

# SSH refuses keys with loose permissions; fix quietly if needed.
perms=$(stat -c "%a" "$PEM" 2>/dev/null || stat -f "%A" "$PEM" 2>/dev/null || echo "")
if [[ "$perms" != "600" && "$perms" != "400" ]]; then
    echo "    Tightening PEM permissions to 600 (was ${perms:-unknown})"
    chmod 600 "$PEM"
fi

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "ERROR: Build directory not found: $BUILD_DIR" >&2
    echo "       Build the WebAssembly target first, or pass -b <dir>." >&2
    exit 1
fi

missing=0
for f in "${REQUIRED_FILES[@]}"; do
    if [[ ! -f "$BUILD_DIR/$f" ]]; then
        echo "ERROR: required file missing from build: $f" >&2
        missing=1
    fi
done
if [[ "$missing" -ne 0 ]]; then
    echo "       Did the WebAssembly build complete successfully?" >&2
    exit 1
fi
echo "    All required files present in $BUILD_DIR"

# ── Connectivity check ─────────────────────────────────────────────
echo "==> Testing SSH connection to $TARGET:$SSH_PORT"
if ! ssh "${SSH_OPTS[@]}" -o ConnectTimeout=10 -o BatchMode=yes "$TARGET" "true" 2>/dev/null; then
    echo "ERROR: cannot SSH to $TARGET" >&2
    echo "       Check the host, the PEM, and that your IP is allowed on port $SSH_PORT." >&2
    exit 1
fi
echo "    SSH OK"

# ── Ensure docroot exists ──────────────────────────────────────────
echo "==> Ensuring docroot exists: $DOCROOT"
ssh "${SSH_OPTS[@]}" "$TARGET" "mkdir -p '$DOCROOT'"

# ── Upload ─────────────────────────────────────────────────────────
# Upload to a temp dir first, then move into place, so a half-finished
# transfer can't leave the live site serving a broken mix of old/new.
STAMP="$(date +%Y%m%d-%H%M%S)"
STAGING="/tmp/dashboard-upload-$STAMP"
echo "==> Uploading build to staging ($STAGING)"
ssh "${SSH_OPTS[@]}" "$TARGET" "mkdir -p '$STAGING'"

for f in "${REQUIRED_FILES[@]}"; do
    echo "    -> $f"
    scp "${SCP_OPTS[@]}" -q "$BUILD_DIR/$f" "$TARGET:$STAGING/$f"
done
for f in "${OPTIONAL_FILES[@]}"; do
    if [[ -f "$BUILD_DIR/$f" ]]; then
        echo "    -> $f (optional)"
        scp "${SCP_OPTS[@]}" -q "$BUILD_DIR/$f" "$TARGET:$STAGING/$f"
    fi
done

# ── Move into place + permissions ──────────────────────────────────
# 644 for files so Apache (and the world) can read them; the docroot
# itself needs 755 so Apache can traverse it (matches the guide's note
# about 403s otherwise).
echo "==> Moving into docroot and setting permissions"
ssh "${SSH_OPTS[@]}" "$TARGET" "
    set -e
    cp -f '$STAGING'/* '$DOCROOT'/
    chmod 755 '$DOCROOT'
    chmod 644 '$DOCROOT'/*
    rm -rf '$STAGING'
"

# ── Verify ─────────────────────────────────────────────────────────
echo "==> Verifying deployment over HTTP"
BASE="http://$REMOTE_HOST"

check_url() {
    local path="$1" expect_type="$2"
    local code ctype
    code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE/$path" || echo "000")
    ctype=$(curl -s -o /dev/null -w "%{content_type}" "$BASE/$path" || echo "")
    printf "    %-22s HTTP %s  %s\n" "$path" "$code" "$ctype"
    if [[ "$code" != "200" ]]; then
        echo "      WARNING: expected 200 for $path" >&2
        return 1
    fi
    if [[ -n "$expect_type" && "$ctype" != *"$expect_type"* ]]; then
        echo "      WARNING: expected content-type containing '$expect_type'" >&2
    fi
    return 0
}

rc=0
check_url "SensorDashboard.html" "text/html"        || rc=1
check_url "SensorDashboard.wasm" "application/wasm"  || rc=1
check_url "SensorDashboard.js"   ""                  || rc=1
check_url "qtloader.js"          ""                  || rc=1
check_url "config.json"          "json"              || rc=1

echo
if [[ "$rc" -eq 0 ]]; then
    echo "Deploy complete. Open: $BASE/  (hard-refresh: Ctrl+Shift+R)"
else
    echo "Deploy finished with warnings — check the messages above." >&2
    echo "Common causes: .wasm MIME not set (AddType application/wasm .wasm)," >&2
    echo "or docroot/file permissions. See the deployment guide §9.1." >&2
fi
exit "$rc"
