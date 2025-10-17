/*
  Nano 33 IoT — Motion Direction Lo// BLE Service and Characteristic


// ================= SMA =================for ML
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

BLEService motionService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLEStringCharacteristic featuresChar("19B10001-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify, 200);

// ================= CONFIG =================
const unsigned long SAMPLE_DT_MS     = 20;   // 50 Hz sampling
const unsigned long REPORT_EVERY_MS  = 100;  // Compute features every 100 ms
const size_t        SMA_LEN          = 2;    // Reduced smoothing to preserve motion details (was 3)
const size_t        WINDOW_SIZE      = 15;   // Reduced window size for faster response (was 20)
const float         THRESHOLD        = 0.05; // Movement detection threshold
// Debug helper: if true, start collecting immediately when armed (skip settle) so you see debug prints
const bool DEBUG_IMMEDIATE_START = false;

// Wrist orientation thresholds (for armed/disarmed state detection)
const float         ARM_THRESHOLD      = 0.4;  // Rotate wrist RIGHT past this (X goes positive) to arm
const float         DISARM_THRESHOLD   = 0.15; // Return past this (X back toward 0) to disarm
const float         DISARMING_ZONE     = 0.45; // Below this = starting to disarm, disable gesture detection
const unsigned long ARM_SETTLE_MS      = 600;  // Wait 700ms after arming before detecting gestures
const unsigned long DISARM_SETTLE_MS   = 500;  // Wait 500ms after disarming to ignore transition

// Set this label before each test motion (e.g. "right", "left", "up", "down") CHANGE THIS BEFORE RUNNING TESTS
String CURRENT_LABEL = "push";

// Set this label to your student id CHANGE THIS BEFORE RUNNING TEST
String STUDENT_ID = "11611553";

// BLE UUIDs (optional if you’re only using Serial)
const char* SERVICE_UUID       = "19B10000-E8F2-537E-4F6C-D104768A1214";
const char* FEATURES_CHAR_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214";

// ================= SMA =================
float smaBufX[SMA_LEN], smaBufY[SMA_LEN], smaBufZ[SMA_LEN];
size_t smaHeadX=0, smaHeadY=0, smaHeadZ=0;
size_t smaCountX=0, smaCountY=0, smaCountZ=0;

// SMA buffers for wrist orientation detection (larger window to reduce noise)
const size_t ORIENT_SMA_LEN = 5;  // Increased from 3 for better noise filtering
float smaOrientX[ORIENT_SMA_LEN];
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
float winGx[WINDOW_SIZE], winGy[WINDOW_SIZE], winGz[WINDOW_SIZE];  // Gyroscope windows
size_t headX=0, headY=0, headZ=0;
size_t headGx=0, headGy=0, headGz=0;  // Gyroscope heads
size_t cntX=0,  cntY=0,  cntZ=0;
size_t cntGx=0, cntGy=0, cntGz=0;  // Gyroscope counts

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

  // Initialize IMU
  if (!IMU.begin()) { 
    Serial.println("IMU init failed!"); 
    while(1); 
  }

  // Initialize BLE
  if (!BLE.begin()) {
    Serial.println("BLE init failed!");
    while(1);
  }

  // Set BLE local name and advertised service
  BLE.setLocalName("Group5");
  BLE.setAdvertisedService(motionService);

  // Add characteristic to service
  motionService.addCharacteristic(featuresChar);

  // Add service
  BLE.addService(motionService);

  // Set initial value
  featuresChar.writeValue("0,0,0,0,0,0,0,0,0,0,still,0");

  // Start advertising
  BLE.advertise();

  Serial.println("meanX,sdX,rangeX,meanY,sdY,rangeY,meanZ,sdZ,rangeZ,meanGx,sdGx,rangeGx,meanGy,sdGy,rangeGy,meanGz,sdGz,rangeGz,label,studentId");
  Serial.println("BLE Active - Device name: Arduino Nano 33 IoT");
}

// ================= Loop =================
void loop() {
  static unsigned long lastSample = 0;
  static unsigned long lastGyroSample = 0;
  static unsigned long lastReport = 0;
  static bool inMotion = false;
  static String motionLabel = "still";
  static bool isArmed = false;  // Wrist state: armed when rotated right
  static unsigned long armTime = 0;  // Time when wrist was armed (for settling)
  static unsigned long disarmTime = 0;  // Time when wrist was disarmed (for settling)
  static float currentSmoothAx = 0;  // Current smoothed X value for disarming detection
  // One-shot collection state: start after arm-settle, collect until disarm, emit once
  static bool collectingWindow = false;
  static bool windowEmitted = false;

  unsigned long now = millis();

  // Handle BLE connections
  BLEDevice central = BLE.central();
  static bool wasConnected = false;
  
  if (central && !wasConnected) {
    wasConnected = true;
    Serial.print("BLE connected: ");
    Serial.println(central.address());
  }
  else if (!central && wasConnected) {
    wasConnected = false;
    Serial.println("BLE disconnected");
  }

  // Sample IMU
  if (now - lastSample >= SAMPLE_DT_MS) {
    lastSample = now;
    float ax, ay, az;
    if (IMU.accelerationAvailable()) {
      IMU.readAcceleration(ax, ay, az);
      
      // Smooth X-axis for wrist state detection
      float smoothAx = smaUpdate(ax, smaOrientX, smaOrientHeadX, smaOrientCountX);
      currentSmoothAx = smoothAx;  // Save for use in feature computation section
      
      // Hysteresis-based wrist state detection
      // When wrist rotates RIGHT (like checking watch), X-axis becomes more positive
      if (!isArmed && smoothAx > ARM_THRESHOLD) {
        isArmed = true;
        armTime = now;  // Record when we armed (for settling period)
        // Reset per-arm state so we can collect a fresh window after settling
        collectingWindow = false;
        windowEmitted = false;
        Serial.print(">>> ARMED - entered arm state, waiting to settle (armTime=");
        Serial.print(armTime);
        Serial.print(") expected ready at ");
        Serial.println(armTime + ARM_SETTLE_MS);
        if (DEBUG_IMMEDIATE_START) {
          collectingWindow = true;
          cntX = cntY = cntZ = 0;
          cntGx = cntGy = cntGz = 0;
          headX = headY = headZ = 0;
          headGx = headGy = headGz = 0;
          Serial.println(">>> DEBUG IMMEDIATE START: collectingWindow=true");
        }
      }
      else if (isArmed && smoothAx < DISARM_THRESHOLD) {
        // Transition: armed -> disarmed. When disarming, if we were collecting a one-shot
        // window, compute features once and emit that sample for ML.
        isArmed = false;
        disarmTime = now;  // Record when we disarmed (for settling period)
        inMotion = false;  // Force end any ongoing motion
        motionLabel = "still";  // Reset label immediately

        if (collectingWindow && !windowEmitted) {
          float meanX, sdX, rangeX;
          float meanY, sdY, rangeY;
          float meanZ, sdZ, rangeZ;
          float meanGx, sdGx, rangeGx;
          float meanGy, sdGy, rangeGy;
          float meanGz, sdGz, rangeGz;
          
          bool okX = computeFeatures(winX, cntX, meanX, sdX, rangeX);
          bool okY = computeFeatures(winY, cntY, meanY, sdY, rangeY);
          bool okZ = computeFeatures(winZ, cntZ, meanZ, sdZ, rangeZ);
          bool okGx = computeFeatures(winGx, cntGx, meanGx, sdGx, rangeGx);
          bool okGy = computeFeatures(winGy, cntGy, meanGy, sdGy, rangeGy);
          bool okGz = computeFeatures(winGz, cntGz, meanGz, sdGz, rangeGz);

          if (okX && okY && okZ && okGx && okGy && okGz) {
            // Emit exactly one CSV line for this arm/disarm cycle labeled with CURRENT_LABEL
            char out[300];
            snprintf(out, sizeof(out),
              "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%s,%s",
              meanX, sdX, rangeX, meanY, sdY, rangeY, meanZ, sdZ, rangeZ,
              meanGx, sdGx, rangeGx, meanGy, sdGy, rangeGy, meanGz, sdGz, rangeGz,
              CURRENT_LABEL.c_str(),
              STUDENT_ID.c_str());

            Serial.println(out);
            BLEDevice central2 = BLE.central();
            if (central2 && central2.connected()) {
              featuresChar.writeValue(out);
            }
          }
        }

        // End collection and mark as emitted so we don't re-emit until next arm
        collectingWindow = false;
        windowEmitted = true;

        // Clear windows ready for next arm
        cntX = cntY = cntZ = 0;
        cntGx = cntGy = cntGz = 0;
        headX = headY = headZ = 0;
        headGx = headGy = headGz = 0;
      }
      
      // Smooth acceleration for motion detection (existing logic)
      float fx = smaUpdate(ax, smaBufX, smaHeadX, smaCountX);
      float fy = smaUpdate(ay, smaBufY, smaHeadY, smaCountY);
      float fz = smaUpdate(az, smaBufZ, smaHeadZ, smaCountZ);
      // Only push into the ML feature window while we're explicitly collecting (until disarm)
      if (collectingWindow && !windowEmitted) {
        pushWin(fx, winX, headX, cntX);
        pushWin(fy, winY, headY, cntY);
        pushWin(fz, winZ, headZ, cntZ);
      }
    }
  }
  
  // Sample Gyroscope
  if (now - lastGyroSample >= SAMPLE_DT_MS) {
    lastGyroSample = now;
    float gx, gy, gz;
    if (IMU.gyroscopeAvailable()) {
      IMU.readGyroscope(gx, gy, gz);
      // Push gyroscope data into windows when collecting
      if (collectingWindow && !windowEmitted) {
        pushWin(gx, winGx, headGx, cntGx);
        pushWin(gy, winGy, headGy, cntGy);
        pushWin(gz, winGz, headGz, cntGz);
      }
    }
  }

  // Compute features periodically (but skip periodic emission while we're in one-shot collection)
  if (now - lastReport >= REPORT_EVERY_MS) {
    lastReport = now;

    // If we're collecting a one-shot window, don't run the periodic compute/emit path.
    // But first check settle status so we can start collection when armed+settled.
    static bool wasSettling = false;
    bool isArmSettled = !isArmed || (now - armTime >= ARM_SETTLE_MS);
    bool isDisarmSettled = (now - disarmTime >= DISARM_SETTLE_MS);
    bool isSettled = isArmSettled && isDisarmSettled;

    // If we're armed and settled and haven't started collecting yet, start collecting.
    if (isArmed && isSettled && !collectingWindow && !windowEmitted) {
      collectingWindow = true;
      // clear any previous data
      cntX = cntY = cntZ = 0;
      cntGx = cntGy = cntGz = 0;
      headX = headY = headZ = 0;
      headGx = headGy = headGz = 0;
      for (size_t i = 0; i < WINDOW_SIZE; i++) { winX[i] = winY[i] = winZ[i] = 0.0f; winGx[i] = winGy[i] = winGz[i] = 0.0f; }
      Serial.println(">>> START COLLECTING one-shot window after arm-settle");
    }

    if (collectingWindow && !windowEmitted) {
      // still collecting -- skip periodic emission
    }
    else {

    float meanX, sdX, rangeX;
    float meanY, sdY, rangeY;
    float meanZ, sdZ, rangeZ;

    bool okX = computeFeatures(winX, cntX, meanX, sdX, rangeX);
    bool okY = computeFeatures(winY, cntY, meanY, sdY, rangeY);
    bool okZ = computeFeatures(winZ, cntZ, meanZ, sdZ, rangeZ);

  if (okX && okY && okZ) {
      // Check if we're in settling period (either after arming or disarming)
      static bool wasSettling = false;
      bool isArmSettled = !isArmed || (now - armTime >= ARM_SETTLE_MS);
      bool isDisarmSettled = (now - disarmTime >= DISARM_SETTLE_MS);
      bool isSettled = isArmSettled && isDisarmSettled;
      
      // Notify when arm settling period completes
      if (isArmed && !wasSettling && !isArmSettled) {
        wasSettling = true;
      }
      else if (isArmed && wasSettling && isArmSettled) {
        // Serial.println(">>> READY - Gesture detection active");  // Commented out for clean CSV data
        wasSettling = false;
      }
      else if (!isArmed) {
        wasSettling = false;
      }
      


  // While collectingWindow is active we do NOT emit periodic lines; we'll emit once on disarm.
  if (!collectingWindow) {
        // Existing motion-detection/telemetry behavior when not in one-shot collection mode.
        // Check if we're in the disarming zone (starting to tilt back to flat)
        bool isDisarming = isArmed && (currentSmoothAx < DISARMING_ZONE);

        // PRIORITY: If we're disarming, immediately end any motion and ignore new motion
        if (isDisarming) {
          if (inMotion) {
            inMotion = false;
            motionLabel = "still";
          }
        }
        // Only detect motion if NOT disarming
        else {
          bool motionNow = (sdX > THRESHOLD || sdY > THRESHOLD || sdZ > THRESHOLD);

          if (motionNow && !inMotion && isArmed && isSettled) {
            // Motion detected AND wrist is armed AND settling period over - record as genuine gesture
            inMotion = true;
            motionLabel = CURRENT_LABEL;
          } 
          else if (!motionNow && inMotion) {
            // Motion ended naturally
            inMotion = false;
            motionLabel = "still";
          }
        }

        // Handle motion when not armed (recentering)
        if (!isArmed && inMotion) {
          inMotion = false;
          motionLabel = "still";
        }

        // Print CSV line with wrist armed status and student ID (periodic telemetry)
        char out[200];
        snprintf(out, sizeof(out),
          "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%s,%s",
          meanX, sdX, rangeX, meanY, sdY, rangeY, meanZ, sdZ, rangeZ,
          isArmed ? 1 : 0,
          motionLabel.c_str(),
          STUDENT_ID.c_str());
        
        // Send to Serial
        Serial.println(out);
        
        // Send to BLE if client is connected
        BLEDevice central = BLE.central();
        if (central && central.connected()) {
          featuresChar.writeValue(out);
        }
      }
    }
  }
}
}
