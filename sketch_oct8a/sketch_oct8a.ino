/*
  Nano 33 IoT — Motion Direction Logger for ML
  --------------------------------------------
  Computes windowed features per axis (X, Y, Z):
  mean, std, range.

  Outputs CSV with a label:
    meanX,sdX,rangeX,meanY,sdY,rangeY,meanZ,sdZ,rangeZ,label

  Movement detection via standard deviation threshold.
  Labels motion as "right", "left", etc. manually (set in CURRENT_LABEL).
  Recenter logic explained below.
*/

#include <Arduino_LSM6DS3.h>
#include <ArduinoBLE.h>
#include <math.h>

// ================= CONFIG =================
const unsigned long SAMPLE_DT_MS     = 20;   // 50 Hz sampling
const unsigned long REPORT_EVERY_MS  = 100;  // Compute features every 100 ms
const size_t        SMA_LEN          = 3;    // Smoothing
const size_t        WINDOW_SIZE      = 20;   // Sliding window for features
const float         THRESHOLD        = 0.015; // Movement detection threshold

// Wrist orientation thresholds (for armed/disarmed state detection)
const float         ARM_THRESHOLD    = 0.3;  // Rotate wrist RIGHT past this (X goes positive) to arm
const float         DISARM_THRESHOLD = 0.1;  // Return past this (X back toward 0) to disarm

// Set this label before each test motion (e.g. "right", "left", "up", "down")
String CURRENT_LABEL = "right";

// BLE UUIDs (optional if you’re only using Serial)
const char* SERVICE_UUID       = "19B10000-E8F2-537E-4F6C-D104768A1214";
const char* FEATURES_CHAR_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214";

// ================= SMA =================
float smaBufX[SMA_LEN], smaBufY[SMA_LEN], smaBufZ[SMA_LEN];
size_t smaHeadX=0, smaHeadY=0, smaHeadZ=0;
size_t smaCountX=0, smaCountY=0, smaCountZ=0;

// SMA buffers for wrist orientation detection (smaller window for responsiveness)
float smaOrientX[3];
size_t smaOrientHeadX=0;
size_t smaOrientCountX=0;

float smaUpdate(float v, float *buf, size_t &head, size_t &count) {
  buf[head] = v;
  head = (head + 1) % SMA_LEN;
  if (count < SMA_LEN) count++;
  float sum = 0;
  for (size_t i = 0; i < count; i++) sum += buf[i];
  return sum / count;
}

// ================= Windows =================
float winX[WINDOW_SIZE], winY[WINDOW_SIZE], winZ[WINDOW_SIZE];
size_t headX=0, headY=0, headZ=0;
size_t cntX=0,  cntY=0,  cntZ=0;

void pushWin(float v, float *win, size_t &head, size_t &cnt) {
  win[head] = v;
  head = (head + 1) % WINDOW_SIZE;
  if (cnt < WINDOW_SIZE) cnt++;
}

bool computeFeatures(const float *win, size_t cnt, float &mean, float &sd, float &range) {
  if (cnt == 0) return false;
  float sum = 0, vmin = win[0], vmax = win[0];
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

// ================= Setup =================
void setup() {
  Serial.begin(115200);
  while(!Serial);

  if (!IMU.begin()) { Serial.println("IMU init failed!"); while(1); }

  Serial.println("meanX,sdX,rangeX,meanY,sdY,rangeY,meanZ,sdZ,rangeZ,wristArmed,label");
  Serial.println("System ready. Rotate wrist right to ARM gesture detection.");
}

// ================= Loop =================
void loop() {
  static unsigned long lastSample = 0;
  static unsigned long lastReport = 0;
  static bool inMotion = false;
  static String motionLabel = "still";
  static bool isArmed = false;  // Wrist state: armed when rotated right

  unsigned long now = millis();

  // Sample IMU
  if (now - lastSample >= SAMPLE_DT_MS) {
    lastSample = now;
    float ax, ay, az;
    if (IMU.accelerationAvailable()) {
      IMU.readAcceleration(ax, ay, az);
      
      // Smooth X-axis for wrist state detection
      float smoothAx = smaUpdate(ax, smaOrientX, smaOrientHeadX, smaOrientCountX);
      
      // Hysteresis-based wrist state detection
      // When wrist rotates RIGHT (like checking watch), X-axis becomes more positive
      if (!isArmed && smoothAx > ARM_THRESHOLD) {
        isArmed = true;
        Serial.println(">>> ARMED - Gesture detection enabled");
      }
      else if (isArmed && smoothAx < DISARM_THRESHOLD) {
        isArmed = false;
        Serial.println(">>> DISARMED - Safe to recenter");
      }
      
      // Smooth acceleration for motion detection (existing logic)
      float fx = smaUpdate(ax, smaBufX, smaHeadX, smaCountX);
      float fy = smaUpdate(ay, smaBufY, smaHeadY, smaCountY);
      float fz = smaUpdate(az, smaBufZ, smaHeadZ, smaCountZ);
      pushWin(fx, winX, headX, cntX);
      pushWin(fy, winY, headY, cntY);
      pushWin(fz, winZ, headZ, cntZ);
    }
  }
  float gx, gy, gz;
  IMU.readGyroscope(gx, gy, gz);  // rotational velocity
  float gyroX = fabs(gx);
  float gyroY = fabs(gy);
  float gyroZ = fabs(gz);

  // Compute features periodically
  if (now - lastReport >= REPORT_EVERY_MS) {
    lastReport = now;

    float meanX, sdX, rangeX;
    float meanY, sdY, rangeY;
    float meanZ, sdZ, rangeZ;

    bool okX = computeFeatures(winX, cntX, meanX, sdX, rangeX);
    bool okY = computeFeatures(winY, cntY, meanY, sdY, rangeY);
    bool okZ = computeFeatures(winZ, cntZ, meanZ, sdZ, rangeZ);

    if (okX && okY && okZ) {
      // Detect motion start/end (only when wrist is armed)
      bool motionNow = (sdX > THRESHOLD || sdY > THRESHOLD || sdZ > THRESHOLD);

      if (motionNow && !inMotion && isArmed) {
        // Motion detected AND wrist is armed - record as genuine gesture
        inMotion = true;
        motionLabel = CURRENT_LABEL;
      } 
      else if (!motionNow && inMotion) {
        // Motion ended
        inMotion = false;
        motionLabel = "still";
      }
      else if (motionNow && !isArmed) {
        // Motion detected but wrist not armed - ignore (this is recentering)
        motionLabel = "still";
      }

      // Print CSV line with wrist armed status
      char out[160];
      snprintf(out, sizeof(out),
        "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%s",
        meanX, sdX, rangeX, meanY, sdY, rangeY, meanZ, sdZ, rangeZ,
        isArmed ? 1 : 0,
        motionLabel.c_str());
      Serial.println(out);
    }
  }
}
