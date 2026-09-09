import json
from pathlib import Path

devices = json.loads(Path("build/devices.json").read_text())["devices"]
candidates = [device for runtime, group in devices.items() if "iOS" in runtime
              for device in group if device.get("isAvailable") and device["name"].startswith("iPhone")]
if not candidates:
    raise SystemExit("No available iPhone simulator on this runner")
device = next((item for item in candidates if item["name"] == "iPhone 16"), candidates[0])
print(f"SIMULATOR_ID={device['udid']}")
