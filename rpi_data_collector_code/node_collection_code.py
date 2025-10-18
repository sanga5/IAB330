# tap_central_simple.py
# Raspberry Pi BLE central for Nano 33 IoT tap notifications
# Requires: bleak  (pip install bleak)

import asyncio
from datetime import datetime
from bleak import BleakScanner, BleakClient, BleakError
from pymongo import MongoClient

TARGET_NAME_KEYWORDS = ["Nano33"]
SERVICE_UUID = "19B10000-E8F2-537E-4F6C-D104768A1214"
CHAR_NOTIFY_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"

MONGODB_URI = "mongodb+srv://shivaan:shivaan@assessmentcluster.xyr5ml6.mongodb.net/?retryWrites=true&w=majority&appName=AssessmentCluster"
DB_NAME = "imu_db"
COLLECTION_NAME = "imu_data"

BATCH_SIZE = 1
buffer = []

def parse_reading(payload: bytes):
    try:
        text = payload.decode("utf-8").strip()
        parts = [p for p in text.replace(" ", "").split(",") if p]
        doc = {"measured_at": datetime.utcnow(), "raw": text}

        if len(parts) == 20:
            meanAx = parts[0]
            sdAx = parts[1]
            rangeAx = parts[2]
            meanAy = parts[3]
            sdAy = parts[4]
            rangeAy = parts[5]
            meanAz = parts[6]
            sdAz = parts[7]
            rangeAz = parts[8]
            meanGx = parts[9]
            sdGx = parts[10]
            rangeGx = parts[11]
            meanGy = parts[12]
            sdGy = parts[13]
            rangeGy = parts[14]
            meanGz = parts[15]
            sdGz = parts[16]
            rangeGz = parts[17]
            label = parts[18]
            studentId = parts[19]
            doc["data"] = [meanAx, sdAx, rangeAx, meanAy, sdAy, rangeAy, meanAz, sdAz, rangeAz, meanGx, sdGx, rangeGx, meanGy, sdGy, rangeGy, meanGz, sdGz, rangeGz, label, studentId]
        else:
            return None
        return doc
    except Exception:
        return None


async def main():
    mongo = MongoClient(MONGODB_URI)
    coll = mongo[DB_NAME][COLLECTION_NAME]
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
            def handle_notify(_sender, data: bytearray):
                global buffer
                msg = data.decode("utf-8", errors="ignore").strip()
                print(f"[notify] {msg}")
                doc = parse_reading(data)
                if doc:
                    buffer.append(doc)
                    if len(buffer) >= BATCH_SIZE:
                        try:
                            coll.insert_many(buffer, ordered=False)
                            print(f"Inserted {len(buffer)} docs")
                        except Exception as e:
                            print(f"Insert failed: {e}")
                        buffer = []
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
