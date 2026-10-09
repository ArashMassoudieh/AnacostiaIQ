#!/usr/bin/env python3
"""Capture and upload Arducam images for the AnacostiaIQ dashboard.

The agent is intentionally separate from the sensor collector: camera or
network failures cannot interrupt measurement collection. It polls the API for
the selected schedule and instant-capture requests, invokes rpicam-still, then
uploads one JPEG with a station-only token.
"""

import argparse
from datetime import datetime, timezone
import json
import logging
import os
from pathlib import Path
import subprocess
import time
import urllib.error
import urllib.request
import uuid


LOG = logging.getLogger("anacostiaiq-camera")


def load_json(path):
    with Path(path).open("r", encoding="utf-8") as fh:
        return json.load(fh)


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, sort_keys=True) + "\n",
                         encoding="utf-8")
    os.replace(temporary, path)


def api_json(url, token, method="GET", body=None, timeout=20):
    encoded = None
    headers = {"X-Camera-Token": token, "Accept": "application/json"}
    if body is not None:
        encoded = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(url, data=encoded, headers=headers,
                                     method=method)
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def multipart_image(url, token, image_path, fields, timeout=60):
    boundary = "----AnacostiaIQ{}".format(uuid.uuid4().hex)
    chunks = []
    for name, value in fields.items():
        chunks.extend([
            "--{}\r\n".format(boundary).encode(),
            ('Content-Disposition: form-data; name="{}"\r\n\r\n'
             .format(name)).encode(),
            str(value).encode(), b"\r\n",
        ])
    chunks.extend([
        "--{}\r\n".format(boundary).encode(),
        b'Content-Disposition: form-data; name="image"; filename="capture.jpg"\r\n',
        b"Content-Type: image/jpeg\r\n\r\n",
        Path(image_path).read_bytes(), b"\r\n",
        "--{}--\r\n".format(boundary).encode(),
    ])
    payload = b"".join(chunks)
    request = urllib.request.Request(
        url, data=payload, method="POST",
        headers={
            "X-Camera-Token": token,
            "Content-Type": "multipart/form-data; boundary={}".format(boundary),
            "Content-Length": str(len(payload)),
            "Accept": "application/json",
        })
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


class CameraAgent:
    def __init__(self, config):
        self.api_url = config["api_url"].rstrip("/")
        self.station_id = config["station_id"]
        self.token = config["token"]
        self.poll_seconds = max(10, int(config.get("poll_seconds", 15)))
        self.width = int(config.get("width", 1920))
        self.height = int(config.get("height", 1080))
        state_dir = Path(config.get(
            "state_dir", "~/.local/state/anacostiaiq-camera")).expanduser()
        state_dir.mkdir(parents=True, exist_ok=True)
        self.image_path = state_dir / "latest.jpg"
        self.state_path = state_dir / "agent-state.json"
        try:
            self.state = load_json(self.state_path)
        except (OSError, ValueError):
            self.state = {}

    def endpoint(self, suffix):
        return "{}/camera/{}/{}".format(
            self.api_url, self.station_id, suffix)

    def capture(self, capture_id, source):
        temporary = self.image_path.with_suffix(".capture.jpg")
        command = [
            "rpicam-still", "-n", "--zsl", "-t", "1000",
            "--width", str(self.width), "--height", str(self.height),
            "-o", str(temporary),
        ]
        LOG.info("Capturing %s image %s", source, capture_id)
        subprocess.run(command, check=True, timeout=45)
        if temporary.stat().st_size < 1024:
            raise RuntimeError("camera produced an unexpectedly small image")
        os.replace(temporary, self.image_path)
        captured_at = datetime.now(timezone.utc).isoformat(timespec="seconds")
        result = multipart_image(
            self.endpoint("upload"), self.token, self.image_path,
            {"capture_id": capture_id, "source": source,
             "captured_at": captured_at})
        LOG.info("Uploaded %s (%s bytes)", capture_id,
                 self.image_path.stat().st_size)
        self.state["last_capture_id"] = capture_id
        self.state["last_capture_at_epoch"] = time.time()
        self.state["last_capture_source"] = source
        atomic_json(self.state_path, self.state)
        return result

    def tick(self):
        pending = api_json(self.endpoint("pending"), self.token)
        request_state = pending.get("request")
        if request_state and request_state.get("id") != \
                self.state.get("last_capture_id"):
            self.capture(request_state["id"], "instant")
            return

        hours = int(pending.get("schedule_hours") or 0)
        if hours <= 0:
            return
        last = float(self.state.get("last_capture_at_epoch") or 0)
        if time.time() - last >= hours * 3600:
            self.capture(uuid.uuid4().hex, "scheduled")

    def run(self, once=False):
        while True:
            try:
                self.tick()
            except urllib.error.HTTPError as error:
                LOG.error("Camera API HTTP %s: %s", error.code,
                          error.read().decode("utf-8", errors="replace"))
            except Exception:
                LOG.exception("Camera agent cycle failed")
            if once:
                return
            time.sleep(self.poll_seconds)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    parser.add_argument("--once", action="store_true")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")
    CameraAgent(load_json(args.config)).run(args.once)


if __name__ == "__main__":
    main()
