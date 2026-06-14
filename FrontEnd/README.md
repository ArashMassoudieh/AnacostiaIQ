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

To change the API URL, edit `SensorDashboard.cpp` line:
```cpp
apiUrl = "http://54.213.147.59:5000";
```

---

## 5. Features
- **Sensor selector** — dropdown with all available sensor types
- **Date range picker** — start / end datetime boxes to filter data
- **Fetch button** — pulls data for selected sensor and range
- **Auto-refresh** — checkbox enables 60-second polling with countdown
- **Status bar** — shows reading count and last update timestamp
