"""
Live SVM Motion Direction Predictor
Receives BLE data from Arduino and predicts movement direction in real-time
"""
import asyncio
import sys
import os
import numpy as np
import joblib
from bleak import BleakClient, BleakScanner
from datetime import datetime

print("🔧 Package versions:")
print(f"   NumPy: {np.__version__}")
print(f"   Joblib: {joblib.__version__}")
try:
    import sklearn
    print(f"   Scikit-learn: {sklearn.__version__}")
except ImportError:
    print("   Scikit-learn: Not available")
print("✅ All packages ready!")

# BLE Service and Characteristic UUIDs (from your Arduino code)
SERVICE_UUID = "19B10000-E8F2-537E-4F6C-D104768A1214"
FEATURES_CHAR_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"

# Load trained model, scaler, and label encoder
def load_model_with_fallback():
    """Load model files with helpful error messages and fallback options"""
    # Try to load the best model first, fall back to SVM
    model_files_priority = [
        ('best_motion_classifier.pkl', 'Best AutoML model'),
        ('svm_motion_classifier.pkl', 'SVM model')
    ]
    
    scaler = None
    model = None
    label_encoder = None
    model_name = None
    
    # Find which model file exists
    for model_file, description in model_files_priority:
        if os.path.exists(model_file):
            model_name = description
            try:
                model = joblib.load(model_file)
                label_encoder = joblib.load('label_encoder.pkl')
                
                # Check if scaler is needed (SVM models typically need it)
                if os.path.exists('feature_scaler.pkl'):
                    try:
                        scaler = joblib.load('feature_scaler.pkl')
                    except:
                        pass
                
                print(f"✅ Loaded {model_name} successfully")
                print(f"📊 Model Type: {type(model).__name__}")
                print(f"🏷️  Labels: {', '.join(label_encoder.classes_)}")
                return model, scaler, label_encoder
                
            except Exception as e:
                print(f"⚠️  Error loading {model_file}: {e}")
                continue
    
    # If we get here, no model was found
    print("❌ No trained model found!")
    print("\n💡 Solutions:")
    print("1. Train a model using: python3 train_best_model.py")
    print("   OR run SVM.ipynb to train SVM model")
    print("2. Make sure you're in: sketch_oct8a/")
    print("3. Check files exist: ls -la *.pkl")
    return None, None, None

# Load the model
import os
svm_model, scaler, label_encoder = load_model_with_fallback()

if svm_model is None:
    print("\n🚫 Cannot proceed without trained model. Exiting...")
    sys.exit(1)

# Prediction statistics
prediction_count = 0
confidence_threshold = 0.7  # Only show predictions above this confidence

def predict_movement_direction(data_string):
    """
    Parse BLE data and predict movement direction
    Input format: meanX,sdX,rangeX,meanY,sdY,rangeY,meanZ,sdZ,rangeZ,wristArmed,label,studentId
    Works with both scaled (SVM) and unscaled (RandomForest, XGBoost) models
    """
    global prediction_count
    
    try:
        # Parse CSV data
        values = data_string.strip().split(',')
        
        if len(values) < 12:
            return None
        
        # Extract the 10 feature values + metadata
        try:
            feature_values = [float(values[i]) for i in range(10)]
            wrist_armed = int(values[9])
            received_label = values[10].strip()
            student_id = values[11].strip()
        except (ValueError, IndexError):
            return None
        
        # Convert to numpy array with proper shape
        features_array = np.array([feature_values], dtype=np.float32)
        
        # Scale features if scaler is available (for SVM models)
        if scaler is not None:
            features_to_predict = scaler.transform(features_array)
        else:
            features_to_predict = features_array
        
        # Make prediction
        prediction_encoded = svm_model.predict(features_to_predict)[0]
        prediction_label = label_encoder.inverse_transform([int(prediction_encoded)])[0]
        
        # Get confidence score (works for most sklearn models)
        confidence = 0.0
        if hasattr(svm_model, 'predict_proba'):
            probabilities = svm_model.predict_proba(features_to_predict)[0]
            confidence = float(max(probabilities))
        elif hasattr(svm_model, 'oob_score_'):
            # For RandomForest, use feature importance as proxy
            confidence = 0.5
        
        prediction_count += 1
        
        # Format output
        timestamp = datetime.now().strftime("%H:%M:%S")
        
        # Color coding for terminal output
        if confidence > confidence_threshold:
            status_icon = "🎯"
        elif confidence > 0.5:
            status_icon = "⚡"
        else:
            status_icon = "❓"
        
        result = {
            'timestamp': timestamp,
            'prediction': prediction_label,
            'received_label': received_label,
            'confidence': confidence,
            'icon': status_icon,
            'features': feature_values,
            'student_id': student_id
        }
        
        return result
        
    except (ValueError, IndexError) as e:
        print(f"⚠️  Error parsing data: {e}")
        return None

def notification_handler(sender, data):
    """
    Handle incoming BLE notifications and predict movement
    """
    try:
        # Decode BLE data
        data_string = data.decode('utf-8').strip()
        
        # Skip empty lines or headers
        if not data_string or 'meanX' in data_string:
            return
            
        # Predict movement direction
        result = predict_movement_direction(data_string)
        
        if result:
            # Display prediction
            print(f"{result['icon']} [{result['timestamp']}] "
                  f"Predicted: {result['prediction'].upper():6} "
                  f"| Actual: {result['received_label']:6} "
                  f"| Confidence: {result['confidence']:.2f} "
                  f"| Student: {result['student_id']}")
            
            # Show feature values occasionally for debugging
            if prediction_count % 10 == 0:
                features_str = ", ".join([f"{f:.3f}" for f in result['features'][:3]])
                print(f"   📊 Sample features (X-axis): [{features_str}...]")
                
    except Exception as e:
        print(f"❌ Error in notification handler: {e}")

async def find_arduino():
    """
    Scan for Arduino Nano 33 IoT device
    Returns the device address or None
    """
    print("🔍 Scanning for Arduino Nano 33 IoT...")
    
    devices = await BleakScanner.discover(timeout=10.0)
    
    print(f"\n📱 Found {len(devices)} BLE devices:")
    for i, device in enumerate(devices, 1):
        print(f"   {i}. {device.name or 'Unknown'} ({device.address})")
    
    # Look for device named "group5"
    for device in devices:
        if device.name and device.name.lower() == "group5":
            print(f"\n✓ Found group5 Arduino: {device.name} ({device.address})")
            return device.address
    
    print("\n❌ Arduino named 'group5' not found.")
    print("💡 Make sure the device name in Arduino code is set to 'group5'")
    return None

async def run_live_prediction(address):
    """
    Connect to Arduino and run live movement prediction
    """
    print(f"🔌 Connecting to Arduino at {address}...")
    
    try:
        async with BleakClient(address) as client:
            if not client.is_connected:
                print("❌ Failed to connect")
                return
            
            print("✅ Connected successfully!")
            
            # Check if our service exists (using newer Bleak API)
            service_found = False
            for service in client.services:
                if service.uuid.lower() == SERVICE_UUID.lower():
                    service_found = True
                    break
            
            if not service_found:
                print(f"❌ Service {SERVICE_UUID} not found on device")
                print("   Available services:")
                for service in client.services:
                    print(f"   - {service.uuid}")
                return
            
            print("🎯 Starting live movement direction prediction...")
            print("💡 Perform movements with your Arduino to see predictions")
            print("⏹️  Press Ctrl+C to stop\n")
            
            # Start notifications
            await client.start_notify(FEATURES_CHAR_UUID, notification_handler)
            
            # Keep running until interrupted
            while True:
                await asyncio.sleep(1)
                
    except Exception as e:
        print(f"❌ Connection error: {e}")

async def main():
    """
    Main function
    """
    print("=" * 60)
    print("  🤖 Live SVM Motion Direction Predictor")
    print("=" * 60)
    print("📡 This tool receives BLE data from Arduino and predicts movement direction")
    print("🎯 Make sure your Arduino is running and broadcasting BLE data\n")
    
    # Find Arduino device
    arduino_address = await find_arduino()
    
    if not arduino_address:
        print("❌ No Arduino device found. Exiting...")
        return
    
    try:
        # Run live prediction
        await run_live_prediction(arduino_address)
        
    except KeyboardInterrupt:
        print(f"\n⏹️  Live prediction stopped")
        print(f"📊 Total predictions made: {prediction_count}")
        print("👋 Goodbye!")
    except Exception as e:
        print(f"❌ Unexpected error: {e}")

if __name__ == "__main__":
    # Pre-flight checks
    print("🚀 Starting Live SVM Motion Direction Predictor")
    print("=" * 60)
    
    # Check if we're in the right directory
    if not os.path.exists('live_predictor.py'):
        print("⚠️  Warning: You might not be in the correct directory")
        print("💡 Navigate to: cd IAB330/sketch_oct8a/")
    
    # Check Bluetooth
    print("🔵 Checking Bluetooth availability...")
    try:
        import bluetooth
        print("✅ Bluetooth support detected")
    except:
        print("⚠️  No bluetooth module found (this is normal on some systems)")
    
    print("\n🎯 Ready to predict motion directions!")
    print("💡 Make sure your Arduino is broadcasting BLE data")
    print("=" * 60)
    
    asyncio.run(main())