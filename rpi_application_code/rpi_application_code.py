import asyncio
from bleak import BleakScanner, BleakClient, BleakError
import joblib
import numpy as np
import warnings
import sys
import os

# Suppress sklearn warnings about version mismatches and feature names
warnings.filterwarnings('ignore')

TARGET_NAME_KEYWORDS = ["Group5"]
SERVICE_UUID = "19B10000-E8F2-537E-4F6C-D104768A1214"
CHAR_NOTIFY_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"

ML_FILENAME = 'svm_model.pkl'
SCALER_FILENAME = 'scaler.pkl'
LABEL_ENCODER_FILENAME = 'label_encoder.pkl'

# Manual label mapping (from SVM.ipynb training)
# label_map = {'right': 0, 'left': 1,  'up': 2, 'down': 3, 'push':4}
LABEL_MAP = {
    0: 'right',
    1: 'left',
    2: 'up',
    3: 'down',
    4: 'push'
}

# Load model and scaler once at startup
print("Loading model and scaler...")
try:
    with warnings.catch_warnings():
        warnings.simplefilter('ignore')
        model = joblib.load(ML_FILENAME)
        scaler = joblib.load(SCALER_FILENAME)
    print("Model and scaler loaded successfully")
except FileNotFoundError as e:
    print(f"Error: Could not find model files: {e}")
    sys.exit(1)

def handle_notify(_sender, data: bytearray):
    global model, scaler
    msg = data.decode("utf-8", errors="ignore").strip()
    print(f"[notify] {msg}")
    
    parts = [p for p in msg.replace(" ", "").split(",") if p]
    
    try:
        # Extract only the first 18 numeric features (skip label and studentId at end)
        if len(parts) < 18:
            print(f"Error: Expected at least 18 features, got {len(parts)}")
            return
        
        # Convert first 18 elements to floats (skip label and studentId)
        feature_values = np.array([float(parts[i]) for i in range(18)], dtype=np.float32)
        features = feature_values.reshape(1, -1)

        # Transform features using scaler
        with warnings.catch_warnings():
            warnings.simplefilter('ignore')
            features_normalized = scaler.transform(features)
        
        # Predict using model
        prediction_encoded = int(model.predict(features_normalized)[0])
        
        # Decode prediction using manual label map
        predicted_label = LABEL_MAP.get(prediction_encoded, f"Unknown_{prediction_encoded}")
        
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
