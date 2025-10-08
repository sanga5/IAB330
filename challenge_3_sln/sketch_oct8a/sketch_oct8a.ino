/*
  Nano 33 IoT — 3-axis SMA + windowed features over BLE
  Self-contained version (no SD)
  Features per axis: mean, std, range
  CSV: label,meanX,sdX,rangeX,meanY,sdY,rangeY,meanZ,sdZ,rangeZ

  Libraries:
    - Arduino_LSM6DS3
    - ArduinoBLE
*/

#include <Arduino_LSM6DS3.h>
#include <ArduinoBLE.h>
#include <math.h>

// ================== CONFIGURATION ==================
const unsigned long SAMPLE_DT_MS     = 50;   // 20 Hz sampling
const unsigned long REPORT_EVERY_MS  = 100;  // feature update rate
const size_t        SMA_LEN          = 5;    // smoothing window
const size_t        WINDOW_SIZE      = 100;  // samples per feature window
const float         THRESHOLD        = 0.10; // std-dev threshold for movement
const char*         CURRENT_LABEL    = "right"; // test direction label
const bool          SERIAL_DEBUG     = true;  // toggle serial prints

// BLE UUIDs (example values)
const char* SERVICE_UUID       = "19B10000-E8F2-537E-4F6C-D104768A1214";
const char* FEATURES_CHAR_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214";

// ================== SMA buffers ==================
float smaBufX[SMA_LEN], smaBufY[SMA_LEN], smaBufZ[SMA_LEN];
size_t smaHeadX = 0, smaCountX = 0;
size_t smaHeadY = 0, smaCountY = 0;
size_t smaHeadZ = 0, smaCountZ = 0;

float smaUpdate(float x, float* buf, size_t& head, size_t& count) {
  buf[head] = x;
  head = (head + 1) % SMA_LEN;
  if (count < SMA_LEN) count++;
  float sum = 0;
  for (size_t i = 0; i < count; i++) sum += buf[i];
  return sum / count;
}

// ================== Sliding windows ==================
float winX[WINDOW_SIZE], winY[WINDOW_SIZE], winZ[WINDOW_SIZE];
size_t headX = 0, headY = 0, headZ = 0;
size_t cntX = 0, cntY = 0, cntZ = 0;

inline void pushWin(float v, float* win, size_t& head, size_t& cnt) {
  win[head] = v;
  head = (head + 1) % WINDOW_SIZE;
  if (cnt < WINDOW_SIZE) cnt++;
}

bool computeFeatures(const float* win, size_t cnt, float& mean, float& sd, float& range) {
  if (cnt == 0) return false;
  float sum = 0;
  float vmin = win[0], vmax = win[0];
  for (size_t i = 0; i < cnt; i++) {
    sum += win[i];
    if (win[i] < vmin) vmin = win[i];
    if (win[i] > vmax) vmax = win[i];
  }
  mean = sum / cnt;
  float varSum = 0;
  for (size_t i = 0; i < cnt; i++) {
    float d = win[i] - mean;
    varSum += d * d;
  }
  sd = sqrtf(varSum / cnt);
  range = vmax - vmin;
  return true;
}

// ================== BLE setup ==================
BLEService featService(SERVICE_UUID);
BLEStringCharacteristic featChar(FEATURES_CHAR_UUID, BLERead | BLENotify, 96);

// ================== Setup ==================
void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  if (!IMU.begin()) {
    Serial.println("IMU init failed");
    while (1) delay(1000);
  }

  if (!BLE.begin()) {
    Serial.println("BLE init failed");
    while (1) delay(1000);
  }

  BLE.setLocalName("Nano33_Motion");
  BLE.setDeviceName("Nano33_Motion");
  BLE.setAdvertisedService(featService);
  featService.addCharacteristic(featChar);
  BLE.addService(featService);
  featChar.writeValue("0,0,0,0,0,0,0,0,0");
  BLE.advertise();

  if (SERIAL_DEBUG) {
    Serial.println("BLE advertising...");
    Serial.print("Label: "); Serial.println(CURRENT_LABEL);
  }
}

// ================== Loop ==================
void loop() {
  BLE.poll();

  static unsigned long lastSampleMs = 0;
  static unsigned long lastReportMs = 0;
  static bool inMotion = false;

  unsigned long now = millis();

  // --- Sampling ---
  if (now - lastSampleMs >= SAMPLE_DT_MS) {
    lastSampleMs += SAMPLE_DT_MS;

    float ax, ay, az;
    if (IMU.accelerationAvailable()) {
      IMU.readAcceleration(ax, ay, az);

      float fx = smaUpdate(ax, smaBufX, smaHeadX, smaCountX);
      float fy = smaUpdate(ay, smaBufY, smaHeadY, smaCountY);
      float fz = smaUpdate(az, smaBufZ, smaHeadZ, smaCountZ);

      pushWin(fx, winX, headX, cntX);
      pushWin(fy, winY, headY, cntY);
      pushWin(fz, winZ, headZ, cntZ);
    }
  }

  // --- Feature reporting ---
  if (now - lastReportMs >= REPORT_EVERY_MS) {
    lastReportMs = now;

    float meanX, sdX, rangeX;
    float meanY, sdY, rangeY;
    float meanZ, sdZ, rangeZ;

    bool okX = computeFeatures(winX, cntX, meanX, sdX, rangeX);
    bool okY = computeFeatures(winY, cntY, meanY, sdY, rangeY);
    bool okZ = computeFeatures(winZ, cntZ, meanZ, sdZ, rangeZ);

    if (okX && okY && okZ) {
      bool moving = (sdX > THRESHOLD || sdY > THRESHOLD || sdZ > THRESHOLD);

      if (moving && !inMotion) {
        inMotion = true;
        if (SERIAL_DEBUG) Serial.println("MOTION_START");
      } else if (!moving && inMotion) {
        inMotion = false;
        if (SERIAL_DEBUG) Serial.println("MOTION_END");
      }

      if (moving) {
        char out[128];
        snprintf(out, sizeof(out),
          "%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",
          CURRENT_LABEL,
          meanX, sdX, rangeX, meanY, sdY, rangeY, meanZ, sdZ, rangeZ);
        featChar.writeValue(out);
        Serial.println(out);
      } else if (SERIAL_DEBUG) {
        Serial.println("No movement");
      }
    }
  }
}
