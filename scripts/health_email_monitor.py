#!/usr/bin/env python3
"""AnacostiaIQ server-side health alarm email monitor.

Polls station-scoped health telemetry from the public API and sends email only
when a component changes alarm state, plus one recovery message when it returns
to healthy. State is persisted locally so process restarts do not resend old
alarms.

SMTP credentials and recipients are intentionally supplied through environment
variables, never committed to the repository.
"""

from __future__ import annotations

import json
import os
import smtplib
import ssl
import sys
import time
from datetime import datetime, timedelta
from email.message import EmailMessage
from pathlib import Path
from typing import Any
from urllib.parse import urlencode
from urllib.request import urlopen

STATE_NAMES = {0: "healthy", 1: "degraded", 2: "offline", 3: "critical"}
ALARM_STATES = {"degraded", "offline", "critical"}
DEFAULT_COMPONENTS = [
    "application",
    "overall",
    "cloud",
    "upload_queue",
    "storage",
    "cpu_temperature",
    "sensor_hcsr04_depth",
    "sensor_maxbotix_depth",
    "sensor_moisture_sensor",
]
COMPONENT_LABELS = {
    "application": "Application",
    "overall": "Overall Health",
    "cloud": "Cloud / API",
    "upload_queue": "Upload Queue",
    "storage": "Storage",
    "cpu_temperature": "CPU Temperature",
    "sensor_hcsr04_depth": "Inflow Weir Head",
    "sensor_maxbotix_depth": "Ponding Depth",
    "sensor_moisture_sensor": "Soil Moisture / ADC",
}


def component_label(component: str) -> str:
    return COMPONENT_LABELS.get(component, component.replace("_", " ").title())


def env_bool(name: str, default: bool = False) -> bool:
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def load_json(path: Path, default: Any) -> Any:
    try:
        return json.loads(path.read_text())
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return default


def save_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    tmp.replace(path)


def parse_csv_env(name: str, default: list[str]) -> list[str]:
    raw = os.getenv(name, "")
    if not raw.strip():
        return default
    return [x.strip() for x in raw.split(",") if x.strip()]


def latest_reading(api_base: str, sensor_id: str, lookback_hours: int) -> dict | None:
    end = datetime.now()
    start = end - timedelta(hours=lookback_hours)
    query = urlencode({"start": start.isoformat(), "end": end.isoformat()})
    url = f"{api_base.rstrip('/')}/sensor/{sensor_id}?{query}"
    with urlopen(url, timeout=10) as response:
        payload = json.loads(response.read().decode("utf-8"))
    if not isinstance(payload, list) or not payload:
        return None
    return max(payload, key=lambda item: str(item.get("timestamp", "")))


def reading_state(reading: dict | None, component: str, stale_seconds: int) -> tuple[str, str, str | None]:
    if reading is None:
        return "unknown", "no telemetry returned", None
    timestamp = reading.get("timestamp")
    try:
        code = int(float(reading.get("value")))
    except (TypeError, ValueError):
        return "unknown", "invalid health state", timestamp
    state = STATE_NAMES.get(code, "unknown")
    if component == "application" and timestamp:
        try:
            ts = datetime.fromisoformat(str(timestamp).replace("Z", "+00:00"))
            now = datetime.now(ts.tzinfo) if ts.tzinfo else datetime.now()
            age = (now - ts).total_seconds()
            if age > stale_seconds:
                return "offline", f"application heartbeat stale ({int(age)} s)", timestamp
        except ValueError:
            pass
    reason = reading.get("reason") or reading.get("unit")
    if not reason or reason == "state":
        reason = f"health state code {code}"
    return state, str(reason), timestamp


def parse_time(value: str | None) -> datetime | None:
    if not value:
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None


def send_email(subject: str, body: str) -> None:
    host = os.environ["ANACOSTIAIQ_SMTP_HOST"]
    port = int(os.getenv("ANACOSTIAIQ_SMTP_PORT", "587"))
    username = os.getenv("ANACOSTIAIQ_SMTP_USERNAME", "")
    password = os.getenv("ANACOSTIAIQ_SMTP_PASSWORD", "")
    sender = os.environ["ANACOSTIAIQ_EMAIL_FROM"]
    recipients = parse_csv_env("ANACOSTIAIQ_EMAIL_TO", [])
    if not recipients:
        raise RuntimeError("ANACOSTIAIQ_EMAIL_TO is empty")

    msg = EmailMessage()
    msg["From"] = sender
    msg["To"] = ", ".join(recipients)
    msg["Subject"] = subject
    msg.set_content(body)

    if env_bool("ANACOSTIAIQ_SMTP_SSL", False):
        with smtplib.SMTP_SSL(host, port, timeout=20, context=ssl.create_default_context()) as smtp:
            if username:
                smtp.login(username, password)
            smtp.send_message(msg)
    else:
        with smtplib.SMTP(host, port, timeout=20) as smtp:
            smtp.ehlo()
            if env_bool("ANACOSTIAIQ_SMTP_STARTTLS", True):
                smtp.starttls(context=ssl.create_default_context())
                smtp.ehlo()
            if username:
                smtp.login(username, password)
            smtp.send_message(msg)


def main() -> int:
    # One-shot SMTP test using the same configuration as real alarm delivery.
    # It intentionally does not read or modify the persisted alert state.
    if "--test-email" in sys.argv[1:]:
        station_name = os.getenv(
            "ANACOSTIAIQ_ALERT_STATION_NAME",
            "John McCormack Field Station",
        )
        station_id = os.getenv(
            "ANACOSTIAIQ_ALERT_STATION_ID",
            "field_station_01",
        )
        location = os.getenv(
            "ANACOSTIAIQ_ALERT_STATION_LOCATION",
            "John McCormack Rd NE · Washington, DC",
        )
        subject = f"[AnacostiaIQ TEST] {station_name}: email alerts"
        body = (
            "AnacostiaIQ email alert test\n\n"
            f"Station: {station_name}\n"
            f"Station ID: {station_id}\n"
            f"Location: {location}\n"
            f"Test time: {datetime.now().isoformat(timespec='seconds')}\n\n"
            "This is a test message. No health alarm was generated.\n\n"
            "Health dashboard: http://54.213.147.59/health.html\n"
        )
        send_email(subject, body)
        print("TEST EMAIL SENT", flush=True)
        return 0

    api_base = os.getenv("ANACOSTIAIQ_API_URL", "http://54.213.147.59:5000")
    station_id = os.getenv("ANACOSTIAIQ_ALERT_STATION_ID", "field_station_01")
    station_name = os.getenv("ANACOSTIAIQ_ALERT_STATION_NAME", "John McCormack Field Station")
    location = os.getenv("ANACOSTIAIQ_ALERT_STATION_LOCATION", "John McCormack Rd NE · Washington, DC")
    components = parse_csv_env("ANACOSTIAIQ_ALERT_COMPONENTS", DEFAULT_COMPONENTS)
    poll_seconds = max(30, int(os.getenv("ANACOSTIAIQ_ALERT_POLL_SECONDS", "60")))
    stale_seconds = max(60, int(os.getenv("ANACOSTIAIQ_APPLICATION_STALE_SECONDS", "600")))
    lookback_hours = max(1, int(os.getenv("ANACOSTIAIQ_ALERT_LOOKBACK_HOURS", "24")))
    repeat_seconds = max(0, int(os.getenv("ANACOSTIAIQ_ALERT_REPEAT_SECONDS", "0")))
    state_path = Path(os.getenv("ANACOSTIAIQ_ALERT_STATE", str(Path.home() / ".local/state/anacostiaiq/email-alert-state.json")))
    enabled = env_bool("ANACOSTIAIQ_EMAIL_ALERTS_ENABLED", False)

    state = load_json(state_path, {"components": {}})
    previous = state.setdefault("components", {})
    first_cycle = not bool(previous)

    print(f"AnacostiaIQ email monitor: station={station_id} poll={poll_seconds}s enabled={enabled}", flush=True)

    while True:
        changed = False
        for component in components:
            sensor_id = f"health_{station_id}_{component}"
            try:
                reading = latest_reading(api_base, sensor_id, lookback_hours)
                current, reason, timestamp = reading_state(reading, component, stale_seconds)
            except Exception as exc:
                print(f"WARN {sensor_id}: {exc}", file=sys.stderr, flush=True)
                continue

            now = datetime.now()
            old_record = previous.get(component, {})
            old = old_record.get("state")
            alarm_since = old_record.get("alarm_since")
            last_notified_at = old_record.get("last_notified_at")
            if current in ALARM_STATES and old not in ALARM_STATES:
                alarm_since = now.isoformat(timespec="seconds")
                last_notified_at = None
            elif current in ALARM_STATES and not alarm_since:
                # Migrate state written by versions predating outage reminders.
                alarm_since = now.isoformat(timespec="seconds")
            elif current not in ALARM_STATES:
                alarm_since = None
                last_notified_at = None
            previous[component] = {
                "state": current,
                "timestamp": timestamp,
                "checked_at": now.isoformat(timespec="seconds"),
                "alarm_since": alarm_since,
                "last_notified_at": last_notified_at,
            }
            changed = True

            # Establish a baseline silently on first launch. Afterwards notify
            # only on transitions, preventing a restart from generating a storm.
            if first_cycle or old is None:
                continue

            is_alarm = current in ALARM_STATES
            is_recovery = old in ALARM_STATES and current == "healthy"
            is_transition = old != current
            last_notified = parse_time(last_notified_at)
            alarm_started = parse_time(alarm_since)
            repeat_anchor = last_notified or alarm_started
            repeat_due = (
                repeat_seconds > 0
                and is_alarm
                and not is_transition
                and repeat_anchor is not None
                and (now - repeat_anchor.replace(tzinfo=None)).total_seconds() >= repeat_seconds
            )
            if not ((is_transition and (is_alarm or is_recovery)) or repeat_due):
                continue

            label = component_label(component)
            if is_recovery:
                subject = f"[AnacostiaIQ RECOVERY] {station_name}: {label} HEALTHY"
                heading = "RECOVERY"
            elif repeat_due:
                subject = f"[AnacostiaIQ REMINDER] {station_name}: {label} {current.upper()}"
                heading = "PROLONGED OUTAGE REMINDER"
            else:
                subject = f"[AnacostiaIQ ALARM] {station_name}: {label} {current.upper()}"
                heading = "ALARM"

            body = (
                f"AnacostiaIQ {heading}\n\n"
                f"Station: {station_name}\n"
                f"Station ID: {station_id}\n"
                f"Location: {location}\n"
                f"Component: {label}\n"
                f"Previous state: {old}\n"
                f"Current state: {current}\n"
                f"Telemetry time: {timestamp or 'unknown'}\n"
                f"Reason: {reason}\n\n"
                f"Health dashboard: http://54.213.147.59/health.html\n"
            )

            if enabled:
                try:
                    send_email(subject, body)
                    print(f"EMAIL {component} ({label}): {old} -> {current}", flush=True)
                    previous[component]["last_notified_at"] = now.isoformat(timespec="seconds")
                except Exception as exc:
                    print(f"ERROR sending {component} alert: {exc}", file=sys.stderr, flush=True)
                    # Do not advance this component's persisted state when mail
                    # fails; retry the transition on the next polling cycle.
                    previous[component]["state"] = old
            else:
                print(f"DRY-RUN {component} ({label}): {old} -> {current} | {subject}", flush=True)
                previous[component]["last_notified_at"] = now.isoformat(timespec="seconds")

        if changed:
            state["station_id"] = station_id
            state["updated_at"] = datetime.now().isoformat(timespec="seconds")
            save_json(state_path, state)
        first_cycle = False
        time.sleep(poll_seconds)


if __name__ == "__main__":
    raise SystemExit(main())
