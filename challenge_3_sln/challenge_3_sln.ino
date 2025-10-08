/*
  Nano 33 IoT — 3-axis SMA + windowed features over BLE
  Features per axis (X,Y,Z): mean, std, range
  Output CSV: meanX,sdX,rangeX,meanY,sdY,rangeY,meanZ,sdZ,rangeZ

  Libraries:
    - Arduino_LSM6DS3
    - ArduinoBLE
*/

#include <Arduino_LSM6DS3.h>
#include <ArduinoBLE.h>
#include <math.h>

// ================== Config ==================
const unsigned long SAMPLE_DT_MS     = 50;   // 50 Hz sampling
const unsigned long REPORT_EVERY_MS  = 100;  // feature push rate
const size_t        SMA_LEN          = 5;    // SMA window for smoothing
const size_t        WINDOW_SIZE      = 100;   // sliding window for features
const float         THRESHOLD        = 0.10; // SD Threshold for recording

// BLE UUIDs (example UUIDs)
const char* SERVICE_UUID       = "19B10000-E8F2-537E-4F6C-D104768A1214";
const char* FEATURES_CHAR_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214";

// ================== SMA state (per-axis) ==================
float smaBufX[SMA_LEN]; 
float smaBufY[SMA_LEN];
float smaBufZ[SMA_LEN];
size_t smaHeadX = 0, smaCountX = 0;
size_t smaHeadY = 0, smaCountY = 0;
size_t smaHeadZ = 0, smaCountZ = 0;

float smaUpdate(float x, float* buf, size_t& head, size_t& count) {
  buf[head] = x;
  head = (head + 1) % SMA_LEN;
  if (count < SMA_LEN) count++;
  float sum = 0.0f;
  for (size_t i = 0; i < count; i++) sum += buf[i];
  return sum / (float)count;
}

// ================== Feature windows (per-axis) ==================
float winX[WINDOW_SIZE], winY[WINDOW_SIZE], winZ[WINDOW_SIZE];
size_t headX = 0, headY = 0, headZ = 0;
size_t cntX = 0,  cntY = 0,  cntZ = 0;

inline void pushWin(float v, float* win, size_t& head, size_t& cnt) {
  win[head] = v;
  head = (head + 1) % WINDOW_SIZE;
  if (cnt < WINDOW_SIZE) cnt++;
}

bool computeFeatures(const float* win, size_t cnt, float& mean, float& sd, float& range) {
  if (cnt == 0) return false;
  float sum = 0.0f;
  for (size_t i = 0; i < cnt; i++) sum += win[i];
  mean = sum / (float)cnt;

  float varSum = 0.0f, vmin = win[0], vmax = win[0];
  for (size_t i = 0; i < cnt; i++) {
    float d = win[i] - mean;
    varSum += d * d;
    if (win[i] < vmin) vmin = win[i];
    if (win[i] > vmax) vmax = win[i];
  }
  sd = sqrtf(varSum / (float)cnt); // population std
  range = vmax - vmin;
  return true;
}

// ================== BLE ==================
BLEService featService(SERVICE_UUID);
BLEStringCharacteristic featChar(FEATURES_CHAR_UUID, BLERead | BLENotify, 96);

// ================== Setup/Loop ==================
void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  if (!IMU.begin()) { Serial.println("IMU init failed"); while (1) delay(1000); }

  if (!BLE.begin()) { Serial.println("BLE init failed"); while (1) delay(1000); }
  BLE.setLocalName("Nano33IoT_3AxisFeat");
  BLE.setDeviceName("Nano33IoT_3AxisFeat");
  BLE.setAdvertisedService(featService);
  featService.addCharacteristic(featChar);
  BLE.addService(featService);
  featChar.writeValue("0,0,0,0,0,0,0,0,0");
  BLE.advertise();
  Serial.println("BLE advertising. Subscribe to features characteristic.");
}

void loop() {
  BLE.poll();

  static unsigned long lastSampleMs = 0;
  static unsigned long lastReportMs = 0;
  unsigned long now = millis();

  // Sample IMU at fixed rate
  if (now - lastSampleMs >= SAMPLE_DT_MS) {
    lastSampleMs += SAMPLE_DT_MS;

    float ax, ay, az;
    if (IMU.accelerationAvailable()) {
      IMU.readAcceleration(ax, ay, az); // m/s^2 on Nano 33 IoT

      // SMA per-axis
      float fx = smaUpdate(ax, smaBufX, smaHeadX, smaCountX);
      float fy = smaUpdate(ay, smaBufY, smaHeadY, smaCountY);
      float fz = smaUpdate(az, smaBufZ, smaHeadZ, smaCountZ);

      // Push filtered values into feature windows
      pushWin(fx, winX, headX, cntX);
      pushWin(fy, winY, headY, cntY);
      pushWin(fz, winZ, headZ, cntZ);
    }
  }

  // Periodic feature computation and BLE notify
  if (now - lastReportMs >= REPORT_EVERY_MS) {
    lastReportMs = now;

    float meanX, sdX, rangeX;
    float meanY, sdY, rangeY;
    float meanZ, sdZ, rangeZ;

    bool okX = computeFeatures(winX, cntX, meanX, sdX, rangeX);
    bool okY = computeFeatures(winY, cntY, meanY, sdY, rangeY);
    bool okZ = computeFeatures(winZ, cntZ, meanZ, sdZ, rangeZ);

    if (okX && okY && okZ) {
      char out[96];
      // CSV: meanX,sdX,rangeX,meanY,sdY,rangeY,meanZ,sdZ,rangeZ
          if (sdX > THRESHOLD || sdY > THRESHOLD || sdZ > THRESHOLD){
      snprintf(out, sizeof(out),
               "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f", "right",
               meanX, sdX, rangeX, meanY, sdY, rangeY, meanZ, sdZ, rangeZ);
      featChar.writeValue(out);
      Serial.println(out);
          }
          else{
            Serial.println("No movement");
          }
    }

  }
}
