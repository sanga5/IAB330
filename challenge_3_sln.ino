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
/*
const unsigned long SAMPLE_DT_MS     = 20;   // 50 Hz sampling
const unsigned long REPORT_EVERY_MS  = 1000;  // feature push rate
const size_t        SMA_LEN          = 5;    // SMA window for smoothing
const size_t        WINDOW_SIZE      = 50;   // sliding window for features
*/
const float TAU = 0.07;  
const float DT  = 0.02;   // Sampling interval ≈20 ms → 50 Hz

const float ALPHA = DT / (TAU + DT);

float yX = 0, yY = 0, yZ = 0;


const int WINDOW_SIZE = 20;

// Buffers to hold the last WINDOW_SIZE samples for each axis
float ax_buffer[WINDOW_SIZE];
float ay_buffer[WINDOW_SIZE];
float az_buffer[WINDOW_SIZE];

int buffer_index = 0;          // Tracks the current position in the buffer
bool buffer_filled = false;    // Becomes true after we fill the buffer at least once

unsigned long last_print_time = 0;
const unsigned long PRINT_INTERVAL = 5000; // Interval between printed outputs (ms)

// BLE UUIDs (example UUIDs)
const char* SERVICE_UUID       = "19B10000-E8F2-537E-4F6C-D104768A1214";
const char* FEATURES_CHAR_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214";

// ================== SMA state (per-axis) ==================
/*
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
*/

/*
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
*/

// ================== BLE ==================
BLEService featService(SERVICE_UUID);
BLEStringCharacteristic featChar(FEATURES_CHAR_UUID, BLERead | BLENotify, 96);

// ================== Setup/Loop ==================
void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  if (!IMU.begin()) { Serial.println("IMU init failed"); while (1) delay(1000); }
  
  float ax, ay, az;
  if (IMU.readAcceleration(ax, ay, az)) {
    yX = ax;
    yY = ay;
    yZ = az;
  }
  Serial.println("rawX filtX rawY filtY rawZ filtZ");

  if (!BLE.begin()) { Serial.println("BLE init failed"); while (1) delay(1000); }
  BLE.setLocalName("Nano33IoT_Group5");
  BLE.setDeviceName("Nano33IoT_Group5");
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
  //static unsigned long lastReportMs = 0;
  unsigned long now = millis();

  float ax, ay, az;

  // Read acceleration (m/s²)
  if (IMU.readAcceleration(ax, ay, az)) {
    // One-pole IIR update
    yX = ALPHA * ax + (1.0 - ALPHA) * yX;
    yY = ALPHA * ay + (1.0 - ALPHA) * yY;
    yZ = ALPHA * az + (1.0 - ALPHA) * yZ;

    // Print raw and filtered values (space-separated)
    Serial.print(ax, 3);
    Serial.print(' ');
    Serial.print(yX, 3);
    Serial.print(' ');
    Serial.print(ay, 3);
    Serial.print(' ');
    Serial.print(yY, 3);
    Serial.print(' ');
    Serial.print(az, 3);
    Serial.print(' ');
    Serial.println(yZ, 3);

    ax_buffer[buffer_index] = yX;
    ay_buffer[buffer_index] = yY;
    az_buffer[buffer_index] = yZ;

    // Move to the next index, wrapping around when we reach the end
    buffer_index++;
    if (buffer_index >= WINDOW_SIZE) {
      buffer_index = 0;
      buffer_filled = true; // Buffer is now full and ready for stats
    }

    if (buffer_filled && millis() - last_print_time >= PRINT_INTERVAL) {
      last_print_time = millis(); // Update timestamp

      // Print computed stats for each axis
      printStats("X-axis", ax_buffer, WINDOW_SIZE);
      printStats("Y-axis", ay_buffer, WINDOW_SIZE);
      printStats("Z-axis", az_buffer, WINDOW_SIZE);
      Serial.println(); // Extra line between blocks for readability
    }


  }

  delay(20);  // ≈50 Hz

  // Sample IMU at fixed rate

  /*
  if (now - lastSampleMs >= DT) {
    lastSampleMs += DT;

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
  */
  

  // Periodic feature computation and BLE notify
  /*
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
      snprintf(out, sizeof(out),
               "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",
               meanX, sdX, rangeX, meanY, sdY, rangeY, meanZ, sdZ, rangeZ);
      featChar.writeValue(out);
      Serial.println(out);
    }
    
  }
  */
}

void printStats(const char* label, float* data, int size) {
  float sum = 0;
  float min_val = data[0];
  float max_val = data[0];

  // Compute sum, min, and max
  for (int i = 0; i < size; i++) {
    sum += data[i];
    if (data[i] < min_val) min_val = data[i];
    if (data[i] > max_val) max_val = data[i];
  }

  float mean = sum / size;

  // Compute variance
  float variance = 0;
  for (int i = 0; i < size; i++) {
    variance += (data[i] - mean) * (data[i] - mean);
  }

  float stddev = sqrt(variance / size);         // Standard deviation
  float range = max_val - min_val;              // Range: max - min

  // Print the results to the Serial Monitor
  Serial.print(label);
  Serial.print(" → mean: ");
  Serial.print(mean, 3);
  Serial.print(", stddev: ");
  Serial.print(stddev, 3);
  Serial.print(", min: ");
  Serial.print(min_val, 3);
  Serial.print(", max: ");
  Serial.print(max_val, 3);
  Serial.print(", range: ");
  Serial.println(range, 3);
}
