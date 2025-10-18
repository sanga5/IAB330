import asyncio
from bleak import BleakScanner, BleakClient, BleakError
import joblib
import numpy as np
import warnings
import sys
import os

# Suppress sklearn warnings about version mismatches and feature names
warnings.filterwarnings('ignore')

TARGET_NAME_KEYWORDS = ["Nano33"]
SERVICE_UUID = "19B10000-E8F2-537E-4F6C-D104768A1214"
CHAR_NOTIFY_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"

ML_FILENAME = 'best_motion_classifier.pkl'
SCALER_FILENAME = 'feature_scaler.pkl'
LABEL_ENCODER_FILENAME = 'label_encoder.pkl'

# Load model and scaler once at startup
print("Loading model, scaler, and label encoder...")
try:
    with warnings.catch_warnings():
        warnings.simplefilter('ignore')
        model = joblib.load(ML_FILENAME)
        scaler = joblib.load(SCALER_FILENAME)
        label_encoder = joblib.load(LABEL_ENCODER_FILENAME)
    print("Model, scaler, and label encoder loaded successfully")
except FileNotFoundError as e:
    print(f"Error: Could not find model files: {e}")
    sys.exit(1)

def handle_notify(_sender, data: bytearray):
    global model, scaler, label_encoder
    msg = data.decode("utf-8", errors="ignore").strip()
    print(f"[notify] {msg}")
    
    parts = [p for p in msg.replace(" ", "").split(",") if p]
    
    try:
        # Extract only the first 18 numeric features
        if len(parts) < 18:
            print(f"Error: Expected at least 18 features, got {len(parts)}")
            return
        
        feature_values = np.array([float(parts[i]) for i in range(18)], dtype=np.float32)
        features = feature_values.reshape(1, -1)

        # Transform features using scaler
        with warnings.catch_warnings():
            warnings.simplefilter('ignore')
            features_normalized = scaler.transform(features)
        
        # Predict using model
        prediction_encoded = model.predict(features_normalized)[0]
        predicted_label = label_encoder.inverse_transform([int(prediction_encoded)])[0]
        print(f"Predicted: {predicted_label}")
    except Exception as e:
        print(f"Error processing data: {e}")

def handle_notify(_sender, data: bytearray):
    global model, scaler, label_encoder
    msg = data.decode("utf-8", errors="ignore").strip()
    print(f"[notify] {msg}")
    
    parts = [p for p in msg.replace(" ", "").split(",") if p]
    
    try:
        # Extract only the first 18 numeric features
        if len(parts) < 18:
            print(f"Error: Expected at least 18 features, got {len(parts)}")
            return
        
        feature_values = np.array([float(parts[i]) for i in range(18)], dtype=np.float32)
        features = feature_values.reshape(1, -1)

        # Transform features using scaler
        with warnings.catch_warnings():
            warnings.simplefilter('ignore')
            features_normalized = scaler.transform(features)
        
        # Predict using model
        prediction_encoded = model.predict(features_normalized)[0]
        predicted_label = label_encoder.inverse_transform([int(prediction_encoded)])[0]
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
