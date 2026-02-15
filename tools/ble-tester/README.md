Chrome BLE tester for Camera Slider

How to run
- Serve this folder over HTTP (Web Bluetooth requires https or localhost):
  - python3 -m http.server 8080
  - then open http://localhost:8080/tools/ble-tester/
- Click “Connect” and pick your device (service filter applies).

What it does
- Subscribes to status notifications (position, flags, travel, state, error).
- Sends commands: Forward, Backward, Stop, Home, Reset error.
- Sets speed %, current mA (uint16 LE), GoTo position (int32 LE).

Device filtering
- By default the Connect dialog filters to devices named Camera_Slider and advertising the slider service UUID.
- Uncheck “Filter to Camera_Slider” to see all BLE devices (still requests access to the slider service).

Notes
- Service/characteristic UUIDs are hardcoded to match firmware.
- Chrome/Edge required; works on desktop with BLE adapter.
