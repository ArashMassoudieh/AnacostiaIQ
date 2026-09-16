#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG_DIR="$HOME/.config/anacostiaiq"
STATE_DIR="$HOME/.local/state/anacostiaiq"
UNIT_DIR="$HOME/.config/systemd/user"
ENV_FILE="$CONFIG_DIR/health-email.env"
UNIT_FILE="$UNIT_DIR/anacostiaiq-health-email.service"

mkdir -p "$CONFIG_DIR" "$STATE_DIR" "$UNIT_DIR"
chmod 700 "$CONFIG_DIR"

if [[ ! -f "$ENV_FILE" ]]; then
  cp "$REPO_ROOT/scripts/health-email.env.example" "$ENV_FILE"
  chmod 600 "$ENV_FILE"
  echo "Created $ENV_FILE"
  echo "Email alerts remain disabled until SMTP settings are filled in and"
  echo "ANACOSTIAIQ_EMAIL_ALERTS_ENABLED=true is set."
else
  echo "Keeping existing $ENV_FILE"
fi

# Materialize the unit with the actual checkout path instead of assuming the
# repository lives directly under the user's home directory.
sed "s|%h/AnacostiaIQ|$REPO_ROOT|g" \
  "$REPO_ROOT/scripts/anacostiaiq-health-email.service" > "$UNIT_FILE"

systemctl --user daemon-reload
systemctl --user enable anacostiaiq-health-email.service

echo
printf '%s\n' "Installed: $UNIT_FILE" "Config:    $ENV_FILE"
echo "After configuring SMTP, test in dry-run mode first:"
echo "  systemctl --user start anacostiaiq-health-email.service"
echo "  journalctl --user -u anacostiaiq-health-email.service -f"
echo "Then set ANACOSTIAIQ_EMAIL_ALERTS_ENABLED=true and restart the service."
