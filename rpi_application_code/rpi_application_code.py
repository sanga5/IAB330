import asyncio
from bleak import BleakScanner, BleakClient, BleakError
import pickle
import numpy as np
import warnings

# Suppress sklearn warnings about version mismatches and feature names
warnings.filterwarnings('ignore')

TARGET_NAME_KEYWORDS = ["Nano33"]
SERVICE_UUID = "19B10000-E8F2-537E-4F6C-D104768A1214"
CHAR_NOTIFY_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"

ML_FILENAME = 'svm_model.pkl'
SCALER_FILENAME = 'scaler.pkl'

# Load model and scaler once at startup
print("Loading model and scaler...")
with open(ML_FILENAME, 'rb') as file:
    model = pickle.load(file)
with open(SCALER_FILENAME, 'rb') as file:
    scaler = pickle.load(file)
print("Model and scaler loaded successfully")

def handle_notify(_sender, data: bytearray):
    global model, scaler
    msg = data.decode("utf-8", errors="ignore").strip()
    print(f"[notify] {msg}")
    
    parts = [p for p in msg.replace(" ", "").split(",") if p]
    
    try:
        # Convert to numpy array with proper dtype for NumPy 2.0.2 compatibility
        feature_values = np.array([float(parts[i]) for i in range(18)], dtype=np.float64)
        features = feature_values.reshape(1, -1)

        features_normalized = scaler.transform(features)
        predicted_label = model.predict(features_normalized)[0]
        print(f"Predicted: {predicted_label}")
    except Exception as e:
        print(f"Error processing data: {e}")

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
