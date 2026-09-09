#!/usr/bin/env bash
set -euo pipefail

# Install per-user AnacostiaIQ startup/recovery integration.
# - headless mode is managed by a systemd user service
# - GUI mode is restored by desktop autostart after graphical login
# - the last selected mode persists across power loss/reboot

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/anacostiaiq"
MODE_FILE="$STATE_DIR/mode"
USER_SYSTEMD_DIR="$HOME/.config/systemd/user"
AUTOSTART_DIR="$HOME/.config/autostart"
SERVICE_FILE="$USER_SYSTEMD_DIR/anacostiaiqd.service"
AUTOSTART_FILE="$AUTOSTART_DIR/anacostiaiq-gui.desktop"

mkdir -p "$STATE_DIR" "$USER_SYSTEMD_DIR" "$AUTOSTART_DIR"
chmod +x "$REPO_DIR/scripts/anacostiaiq-mode"

# On first installation, preserve what is actually running now.
if [[ ! -f "$MODE_FILE" ]]; then
    if pgrep -x AnacostiaIQ >/dev/null 2>&1; then
        printf 'gui\n' > "$MODE_FILE"
        echo "Detected running GUI; initial saved mode = gui"
    elif pgrep -x anacostiaiqd >/dev/null 2>&1; then
        printf 'headless\n' > "$MODE_FILE"
        echo "Detected running headless monitor; initial saved mode = headless"
    else
        printf 'headless\n' > "$MODE_FILE"
        echo "No running AnacostiaIQ process detected; initial saved mode = headless"
    fi
fi

# Expand the repository path in templates. Escape characters meaningful in
# sed replacement strings so paths containing '&' or '\\' remain valid.
SED_REPO=${REPO_DIR//\\/\\\\}
SED_REPO=${SED_REPO//&/\\&}
sed "s|@REPO_DIR@|$SED_REPO|g" \
    "$REPO_DIR/deploy/systemd/anacostiaiqd.service.in" > "$SERVICE_FILE"
sed "s|@REPO_DIR@|$SED_REPO|g" \
    "$REPO_DIR/deploy/autostart/anacostiaiq-gui.desktop.in" > "$AUTOSTART_FILE"

systemctl --user daemon-reload
systemctl --user enable anacostiaiqd.service

# Linger allows the user's headless service to start at boot even before an
# interactive login. This is required for unattended field operation.
if command -v loginctl >/dev/null 2>&1; then
    echo "Enabling systemd user lingering for $USER (sudo may ask for your password)..."
    sudo loginctl enable-linger "$USER"
fi

case "$(tr -d '[:space:]' < "$MODE_FILE")" in
    gui)
        systemctl --user stop anacostiaiqd.service || true
        echo "Saved mode is GUI. It will be restored at graphical login."
        ;;
    *)
        printf 'headless\n' > "$MODE_FILE"
        systemctl --user restart anacostiaiqd.service
        echo "Saved mode is headless. anacostiaiqd is running under systemd."
        ;;
esac

echo
echo "Installed AnacostiaIQ startup recovery."
echo "Current mode: $($REPO_DIR/scripts/anacostiaiq-mode status)"
echo "Switch to GUI:      $REPO_DIR/scripts/anacostiaiq-mode gui"
echo "Switch to headless: $REPO_DIR/scripts/anacostiaiq-mode headless"
echo "Run system check:   $REPO_DIR/scripts/anacostiaiq-mode check"
echo "Headless status:    systemctl --user status anacostiaiqd.service"
