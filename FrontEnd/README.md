# SmartRainHarvest Sensor Dashboard — Build Guide

## Overview
Qt-based dashboard that visualizes sensor data from the EC2 Flask API.
Can be compiled natively (for testing) or as **WebAssembly** to run in a browser.

---

## 1. Desktop Build (for testing)

```bash
cd SensorDashboard
mkdir build && cd build
qmake ..
make -j$(nproc)
./SensorDashboard
```

---

## 2. WebAssembly Build

### Prerequisites
1. **Emscripten SDK** (3.1.25 or later recommended):
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh
   ```

2. **Qt 6 for WebAssembly** — install via the Qt Online Installer:
   - In the installer, select your Qt version (e.g. 6.6) → check **WebAssembly (multi-threaded)** or **WebAssembly (single-threaded)**.
   - This installs a Wasm-specific Qt kit, typically at:
     `~/Qt/6.6.0/wasm_singlethread/` or `~/Qt/6.6.0/wasm_multithread/`

### Build Steps

```bash
cd SensorDashboard
mkdir build-wasm && cd build-wasm

# Point to the Wasm Qt kit's qmake
~/Qt/6.6.0/wasm_singlethread/bin/qmake ..
make -j$(nproc)
```

This produces:
- `SensorDashboard.html`  — entry page
- `SensorDashboard.js`    — glue code
- `SensorDashboard.wasm`  — compiled binary
- `qtloader.js`           — Qt's Wasm loader

### Serve Locally
```bash
# Python simple server (for testing)
python3 -m http.server 8080
```
Then open `http://localhost:8080/SensorDashboard.html` in your browser.

### Deploy to a Web Server
Copy the 4 files above to any static file host (S3, nginx, Apache, GitHub Pages, etc.).

---

## 2b. Site Layout and the Landing Page

The public site is two pages:

| URL                     | File                    | Source                  |
|-------------------------|-------------------------|-------------------------|
| `/`                     | `index.html`            | `web/index.html` (tracked) |
| `/SensorDashboard.html` | the WebAssembly charts  | build output (generated)   |
| `/config.json`          | runtime chart config    | `config.json` (tracked)    |

`web/index.html` is a hand-written, self-contained project description that
links to the dashboard. Keep it self-contained: the vhost sets
`Cross-Origin-Embedder-Policy: require-corp`, which blocks cross-origin
subresources, so no CDN fonts, stylesheets, or remote images will load.

Serving `index.html` at `/` requires the vhost to list it:

```apache
DirectoryIndex index.html SensorDashboard.html
```

The deployment guide's original vhost names only `SensorDashboard.html`, which
is why `/` used to open straight onto the charts.
`deploy_dashboard.sh` rewrites that line for you (idempotently, with a backup
and a `configtest` before reloading) as long as the `ubuntu` user has
passwordless `sudo`. If it doesn't, the script prints the one-liner to run by
hand; re-run with `-n` afterwards to skip the step.

### Deploying

```bash
./deploy_dashboard.sh -i ~/keys/ArashLinux.pem
```

Run it from this directory — it reads the Qt output from
`build/WebAssembly_Qt_6_8_2_single_threaded-Release/` and the tracked sources
(`web/index.html`, `config.json`) from here. Both paths are overridable (`-b`,
`-s`), as are the host, docroot, and vhost path. It verifies over HTTP
afterwards, including a check that `/` actually returns the landing page rather
than the charts.

That default build path has a Qt version and threading mode in it, because
that is how Qt Creator names the directory — a different machine or kit will
produce a different name. If the default is missing, the script globs `./build`
for a single `*WebAssembly*Release*` directory and uses it, and refuses with a
list if there is more than one. Pass `-b` to settle it explicitly.

Either threading mode deploys fine. The vhost already sets the
`Cross-Origin-Opener-Policy` / `Cross-Origin-Embedder-Policy` pair that
multi-threaded Wasm requires; on a single-threaded build those headers are
simply unused (they do still block cross-origin subresources, which is why the
landing page is self-contained).

Note which `config.json` ships: the **tracked** one in this directory, not the
copy in the build tree. `qmake` copies the tracked file into the build
directory on every build (see `SensorDashboard.pro`) so desktop runs pick up
edits too, but the deploy takes the source directly. That way the file's whole
premise — change sensors or colours without recompiling — holds even when you
edit and redeploy without rebuilding.

---

## 3. Flask API Requirements

The dashboard expects these endpoints on your EC2 server:

| Method | Endpoint                  | Description                      |
|--------|---------------------------|----------------------------------|
| GET    | `/sensors`                | Returns JSON array of sensor IDs |
| GET    | `/sensor/<id>?start=&end=`| Returns readings in date range   |
| POST   | `/sensor`                 | (existing) Stores a reading      |

**CORS** must be enabled — install `flask-cors`:
```bash
source /home/ubuntu/sensor-api/venv/bin/activate
pip install flask-cors
```

See `flask_api_additions.py` for the exact code to add.

---

## 4. Configuration

Everything runtime-tunable lives in `config.json` — no recompile. Edit it and
redeploy; the deploy ships the tracked file directly.

```json
"api_url": "http://54.213.147.59:5000",
"refresh_interval_sec": 60,
"sensors": [ ... ]
```

**Sensor ids must match what the Pi publishes.** A non-empty `sensors` array is
treated as authoritative and suppresses `GET /sensors` discovery entirely
(`hasExplicitSensorList()` in `DashboardConfig`), so an id with no data behind
it draws a permanently empty chart rather than being skipped. Check the live
list before adding one:

```bash
curl -s http://54.213.147.59:5000/sensors
```

Use `"visible": false` to keep a sensor on file — documented, coloured, ready
to re-enable — without charting it while it has no data.

---

## 5. Features
- **Sensor selector** — dropdown with all available sensor types
- **Date range picker** — start / end datetime boxes to filter data
- **Fetch button** — pulls data for selected sensor and range
- **Auto-refresh** — checkbox enables 60-second polling with countdown
- **Status bar** — shows reading count and last update timestamp
