"""
BLE Data Collector for Arduino Nano 33 IoT
Reads motion data via BLE and saves to CSV for ML training
"""

import asyncio
import csv
import sys
from datetime import datetime
from bleak import BleakClient, BleakScanner

# BLE Service and Characteristic UUIDs (from your Arduino code)
SERVICE_UUID = "19B10000-E8F2-537E-4F6C-D104768A1214"
FEATURES_CHAR_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"

# CSV file to save data
OUTPUT_FILE = f"motion_data_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"

# CSV header (matches your Arduino output)
CSV_HEADER = ["meanX", "sdX", "rangeX", "meanY", "sdY", "rangeY", 
              "meanZ", "sdZ", "rangeZ", "wristArmed", "label", "studentId"]

# Global variables
csv_writer = None
csv_file = None
data_count = 0


def notification_handler(sender, data):
    """
    Handle incoming BLE notifications
    Decode the data and write to CSV
    """
    global csv_writer, data_count
    
    try:
        # Decode bytes to string
        decoded = data.decode('utf-8').strip()
        
        # Skip empty lines or header lines
        if not decoded or decoded.startswith('mean') or decoded.startswith('>>>'):
            return
        
        # Split CSV line into values
        values = decoded.split(',')
        
        # Validate we have the right number of columns (12)
        if len(values) != 12:
            print(f"⚠️  Skipping malformed line (expected 12 columns, got {len(values)}): {decoded}")
            return
        
        # Write to CSV
        csv_writer.writerow(values)
        data_count += 1
        
        # Print progress every 10 samples
        if data_count % 10 == 0:
            print(f"✓ Collected {data_count} samples... (Label: {values[10]})")
        
    except Exception as e:
        print(f"❌ Error processing data: {e}")
        print(f"   Raw data: {data}")


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


async def collect_data(address):
    """
    Connect to Arduino and collect motion data via BLE
    """
    global csv_writer, csv_file
    
    print(f"\n📡 Connecting to {address}...")
    
    async with BleakClient(address) as client:
        if not client.is_connected:
            print("❌ Failed to connect")
            return
        
        print(f"✓ Connected to Arduino\n")
        
        # Check if our service exists (Bleak 1.1.0 uses .services property)
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
        
        # Open CSV file
        csv_file = open(OUTPUT_FILE, 'w', newline='')
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(CSV_HEADER)
        print(f"💾 Saving data to: {OUTPUT_FILE}\n")
        
        # Subscribe to notifications
        print("📊 Starting data collection...")
        print("   Arm the device (rotate wrist) to start recording gestures")
        print("   Press Ctrl+C to stop\n")
        
        await client.start_notify(FEATURES_CHAR_UUID, notification_handler)
        
        try:
            # Keep collecting until user stops
            while True:
                await asyncio.sleep(1)
                
        except KeyboardInterrupt:
            print("\n\n⏹️  Stopping data collection...")
        
        finally:
            # Stop notifications
            await client.stop_notify(FEATURES_CHAR_UUID)
            
            # Close CSV file
            if csv_file:
                csv_file.close()
            
            print(f"\n✅ Data collection complete!")
            print(f"   Total samples: {data_count}")
            print(f"   Saved to: {OUTPUT_FILE}")


async def main():
    """
    Main entry point
    """
    print("=" * 60)
    print("  Arduino Motion Data Collector (BLE)")
    print("=" * 60)
    
    # Find Arduino device
    address = await find_arduino()
    
    if not address:
        print("\n💡 Tip: Make sure your Arduino sketch includes BLE.advertise()")
        return
    
    # Collect data
    try:
        await collect_data(address)
    except Exception as e:
        print(f"\n❌ Error: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n\n👋 Goodbye!")
        sys.exit(0)
