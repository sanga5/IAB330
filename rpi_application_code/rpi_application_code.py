import asyncio
from bleak import BleakScanner, BleakClient, BleakError
import pickle
import numpy as np


TARGET_NAME_KEYWORDS = ["Nano33"]
SERVICE_UUID = "19B10000-E8F2-537E-4F6C-D104768A1214"
CHAR_NOTIFY_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"

ML_FILENAME = 'svm_model.pkl'
SCALER_FILENAME = 'scaler.pkl'

def handle_notify(_sender, data: bytearray):
    msg = data.decode("utf-8", errors="ignore").strip()
    print(f"[notify] {msg}")
    with open(ML_FILENAME, 'rb') as file:
        model = pickle.load(file)
    with open(SCALER_FILENAME, 'rb') as file:
        scaler = pickle.load(file)
    parts = [p for p in text.replace(" ", "").split(",") if p]
    
    features = np.array([float(parts[0]), float(parts[1]), float(parts[2]), float(parts[3]), float(parts[4]), float(parts[5]),
                         float(parts[6]), float(parts[7]), float(parts[8]), float(parts[9]), float(parts[10]), float(parts[11]),
                         float(parts[12]), float(parts[13]), float(parts[14]), float(parts[15]), float(parts[16]), float(parts[17])]).reshape(1, -1)

    features_normalized = scaler.transform(features)
    predicted_label = model.predict(features_normalized)[0]
    print(predicted_label)

async def main():
    print(f"Scanning...")
    devices = await BleakScanner.discover()

    target = None
    for d in devices:
        if d.name and any(k in d.name for k in TARGET_NAME_KEYWORDS):
            target = d
            break
    if target is None:
        print("No matching device found.")
        return

    print(f"Connecting to {target.name}")
    try:
        async with BleakClient(target.address) as client:
            if not client.is_connected:
                raise BleakError("Connection failed")

            print("Connected. Subscribing to notifications…")
            await client.start_notify(CHAR_NOTIFY_UUID, handle_notify)
            print("Retrieving data (Ctrl+C to stop)")
            while True:
                await asyncio.sleep(1.0)
    except KeyboardInterrupt:
        print("\nStopped by user.")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    asyncio.run(main())
