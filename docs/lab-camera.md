# Lab Arducam B0647 camera

The CUA lab station uses an Arducam B0647 low-light camera on the Raspberry
Pi 5 CAM/DISP connector nearest the Ethernet jack. The working Pi device-tree
selection calls that connector `cam0`.

## Verified camera configuration

With Pi power disconnected, insert the narrow 22-pin ribbon end into the Pi.
Its exposed contacts face the Ethernet jack. The wide 15-pin end goes into the
camera board. Leave the camera's red/black motorized IR-cut lead connected to
its small white socket; it does not connect to Pi GPIO.

In `/boot/firmware/config.txt`:

```ini
camera_auto_detect=0

[all]
dtoverlay=imx290,clock-frequency=37125000,cam0
```

Remove stale OV5647/IMX462 camera overlays, reboot, and verify:

```bash
rpicam-hello --list-cameras
rpicam-still -n --zsl -t 1000 --width 1920 --height 1080 \
  -o /tmp/b0647-test.jpg
```

The verified unit reports `imx290 [1920x1080 12-bit RGGB]` and supports
1280x720 and 1920x1080 modes at up to 60 fps.

## Capture and upload service

`scripts/camera_agent.py` is a separate user service. Camera failures therefore
do not stop `anacostiaiqd` or sensor uploads. The agent:

1. polls the protected camera endpoint every 15 seconds;
2. follows the dashboard schedule (`off`, 1, 6, 12 or 24 hours);
3. services instant-capture requests;
4. captures a 1920x1080 JPEG with `rpicam-still`; and
5. uploads it with the private `X-Camera-Token` header.

Install it for the logged-in Pi user after receiving the server token in a
local file:

```bash
scripts/install_camera_agent.sh --token-file /secure/path/camera-token
```

The generated config is `~/.config/anacostiaiq/camera.json` with mode 0600.
Never commit that file or the token. Inspect the service with:

```bash
systemctl --user status anacostiaiq-camera.service
journalctl --user -u anacostiaiq-camera.service -n 50
```

## Server and dashboard flow

The tracked Flask service in `server/app.py` provides:

| Method | Endpoint | Use |
|---|---|---|
| `GET` | `/camera/<station>/state` | dashboard metadata and latest image URL |
| `POST` | `/camera/<station>/request` | queue an instant capture (rate limited) |
| `PUT` | `/camera/<station>/schedule` | select the automatic interval |
| `GET` | `/camera/<station>/pending` | protected Pi polling |
| `POST` | `/camera/<station>/upload` | protected multipart JPEG upload |

The server stores the public lab images below
`/home/ubuntu/dashboard/lab/camera/<station>/`. `latest.jpg` is replaced
atomically. Timestamped archive images are retained for 30 days with a maximum
of 500 files. The lab dashboard shows the latest image, timestamp, schedule
selector and **Capture now** button.

The current dashboard control endpoints are intentionally available from the
public lab page; capture requests are rate limited. If the dashboard later
contains sensitive views or is broadly advertised, put `/lab/` and the two
control endpoints behind authentication.

## Video

The hardware can also record video. This ten-second diagnostic records H.264:

```bash
rpicam-vid -n -t 10000 --width 1920 --height 1080 --framerate 30 \
  -o /tmp/b0647-test.h264
```

Video upload and playback are not enabled in the dashboard yet. Short clips
can reuse the same request/upload design, with separate storage and stricter
retention limits.
