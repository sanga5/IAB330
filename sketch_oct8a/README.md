# BLE Motion Data Collector

Python script to collect motion data from Arduino Nano 33 IoT via Bluetooth Low Energy (BLE).

## Setup

1. Install dependencies:
```bash
pip install -r requirements.txt
```

## Usage

1. **Upload Arduino sketch** to your Nano 33 IoT
2. **Set the label** in Arduino code:
   ```cpp
   String CURRENT_LABEL = "right";  // Change to: right, left, up, down, still
   ```
3. **Run the Python script**:
   ```bash
   python ble_data_collector.py
   ```

4. **Collect data**:
   - Script will auto-detect your Arduino
   - Arm the device (rotate wrist right)
   - Perform your gesture multiple times
   - Press `Ctrl+C` when done

5. **Change labels and repeat**:
   - Update `CURRENT_LABEL` in Arduino code
   - Re-upload sketch
   - Run script again to collect different gesture

## Output

Data is saved to: `motion_data_YYYYMMDD_HHMMSS.csv`

CSV format:
```
meanX,sdX,rangeX,meanY,sdY,rangeY,meanZ,sdZ,rangeZ,wristArmed,label,studentId
1.0012,0.0234,0.0567,-0.0987,0.0123,0.0345,-1.0234,0.0156,0.0456,1,right,11611553
```

## Combining Multiple Files

To combine all CSV files for training:

```bash
# Linux/Mac
cat motion_data_*.csv | grep -v "^meanX" > combined_training_data.csv

# Or use Python
python -c "
import pandas as pd
import glob

files = glob.glob('motion_data_*.csv')
dfs = [pd.read_csv(f) for f in files]
combined = pd.concat(dfs, ignore_index=True)
combined.to_csv('combined_training_data.csv', index=False)
"
```

## Tips

- Collect 50-100 samples per gesture for good training
- Make gestures varied (fast, slow, big, small)
- Include some "still" samples while armed
- Device automatically ignores recentering motion (wrist flat)

## Troubleshooting

**Arduino not found:**
- Check Arduino is powered on
- Ensure BLE is initialized in sketch
- Try moving closer to computer

**Connection fails:**
- Restart Arduino
- Check Bluetooth is enabled on computer
- Try running script as administrator/sudo (Linux)

**Malformed data:**
- Check Serial Monitor shows clean CSV output
- Ensure no debug messages are uncommented
- Verify STUDENT_ID is set correctly
