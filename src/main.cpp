
#include "Arduino.h"
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps_V6_12.h"
#include "Wire.h"
#include "ArduinoOTA.h"
#include "keys.h"
#include "math.h"
#include "motor.h"
#include "PID_v1.h"
#define REMOTEXY_MODE__ESP32CORE_BLE
#include <RemoteXY.h>

// RemoteXY connection settings 
#define REMOTEXY_BLUETOOTH_NAME "SelfBalancingRobot"
#pragma pack(push, 1)
uint8_t RemoteXY_CONF[] =
  { 255,10,0,12,0,164,0,10,13,4,
  131,1,1,1,22,7,1,2,31,67,
  111,110,116,114,111,108,108,0,131,0,
  26,1,20,7,2,2,31,83,116,97,
  116,117,115,0,68,18,4,15,93,45,
  2,8,36,135,1,0,4,47,12,12,
  1,2,31,88,0,66,132,83,3,13,
  10,2,2,24,129,0,3,15,5,4,
  1,17,107,112,0,129,0,4,24,3,
  4,1,17,107,105,0,129,0,3,32,
  5,4,1,17,107,100,0,7,52,74,
  15,20,5,1,2,26,2,7,52,74,
  23,20,5,1,2,26,2,7,52,74,
  31,20,5,1,2,26,2,65,7,83,
  49,9,9,1,4,128,9,15,63,5,
  1,2,26,4,128,9,23,63,5,1,
  2,26,4,128,9,31,63,5,1,2,
  26 };
  

MPU6050 mpu; 
#define MPU_INTERRUPT_PIN 19  // use pin 2 on Arduino Uno & most boards

// MPU control/status vars
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
Quaternion q;           // [w, x, y, z]         quaternion container
VectorInt16 aa;         // [x, y, z]            accel sensor measurements
VectorInt16 gy;         // [x, y, z]            gyro sensor measurements
VectorInt16 aaReal;     // [x, y, z]            gravity-free accel sensor measurements
VectorInt16 aaWorld;    // [x, y, z]            world-frame accel sensor measurements
VectorFloat gravity;    // [x, y, z]            gravity vector
float euler[3];         // [psi, theta, phi]    Euler angle container
float ypr[3];           // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector

static volatile bool mpuInterrupt = false;
void IRAM_ATTR dmpDataReady() {
  mpuInterrupt = true;
}

#define MOTOR_A1 32
#define MOTOR_A2 33
#define MOTOR_B1 26
#define MOTOR_B2 25
 

double targetAngle = 0;
double inputAngle;
double pidOutput;
double speed;
double const initialPidKp=1, initialPidKi = 0, initialPikKd = 0; 
double pidKp=initialPidKp, pidKi=initialPidKi, pidKd=initialPikKd;

//Specify the links and initial tuning parameters
PID pid(&inputAngle, &pidOutput, &targetAngle, pidKp, pidKi, pidKd, DIRECT);


int16_t rPidKpEdit;  // 32767.. +32767 
int16_t rPidKiEdit;  // 32767.. +32767 
int16_t rPidKdEdit;  // 32767.. +32767 
int8_t rPidKp; // =0..100 slider position 
int8_t rPidKi; // =0..100 slider position 
int8_t rPidKd; // =0..100 slider position 
static unsigned long loopSum = 0;
static unsigned int loopCount = 0;
bool enginesOn = true;

// this structure defines all the variables and events of your control interface 
struct {

    // input variables
  uint8_t butttonStop; // =1 if button pressed, else =0 
  int16_t pidKpEdit;  // 32767.. +32767 
  int16_t pidKiEdit;  // 32767.. +32767 
  int16_t pidKdEdit;  // 32767.. +32767 
  int8_t pidKp; // =0..100 slider position 
  int8_t pidKi; // =0..100 slider position 
  int8_t pidKd; // =0..100 slider position 

    // output variables
  float graph_var1;
  float graph_var2;
  int8_t angle; // =0..100 level position 
  uint8_t ledState_r; // =0..255 LED Red brightness 
  uint8_t ledState_g; // =0..255 LED Green brightness 
  uint8_t ledState_b; // =0..255 LED Blue brightness 

    // other variable
  uint8_t connect_flag;  // =1 if wire connected, else =0 

} RemoteXY;
#pragma pack(pop)

// ================================================================
// ===                      INITIAL SETUP                       ===
// ================================================================

void setupMPU6050() {  
  Serial.println(F("Initializing MPU6050"));
  mpu.initialize();
  pinMode(MPU_INTERRUPT_PIN, INPUT);
  Serial.println(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));
  // load and configure the DMP
  Serial.println(F("Initializing DMP..."));
  devStatus = mpu.dmpInitialize();
  mpu.setXGyroOffset(142);
  mpu.setYGyroOffset(18);
  mpu.setZGyroOffset(5);
  mpu.setXAccelOffset(-3500);
  mpu.setYAccelOffset(-868);
  mpu.setZAccelOffset(1660);
  // make sure it worked (returns 0 if so)
  if (devStatus == 0) {
    // Calibration Time: generate offsets and calibrate our MPU6050
    // mpu.CalibrateAccel(6);
    // mpu.CalibrateGyro(6);
    // Serial.println();
    // mpu.PrintActiveOffsets();

    Serial.println(F("Enabling DMP..."));
    mpu.setDMPEnabled(true);
    attachInterrupt(digitalPinToInterrupt(MPU_INTERRUPT_PIN), dmpDataReady, RISING);
    mpuIntStatus = mpu.getIntStatus();
    Serial.println(F("DMP ready! Waiting for first interrupt..."));
    dmpReady = true;
    packetSize = mpu.dmpGetFIFOPacketSize();
  } else {
    // ERROR!
    // 1 = initial memory load failed
    // 2 = DMP configuration updates failed
    // (if it's going to break, usually the code will be 1)
    Serial.print(F("DMP Initialization failed (code "));
    Serial.print(devStatus);
    Serial.println(F(")"));
  }
}

void setupWifi() {
  Serial.println("Connecting to wifi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(MYSSID, MYPASS);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(2000);
    ESP.restart();
  }
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}


void setupOTA() {
  ArduinoOTA.setHostname("BalancingBotESP32");
  ArduinoOTA
    .onStart([]() {
      motorsStop();
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();
}

void waitForOTA() {
  Serial.println("Waiting for OTA for 10 sec");
  unsigned long start = millis();
  while(millis() - start <= 10000) {
      Serial.print(".");
      ArduinoOTA.handle();
      delay(500);
  }
}

void setupBluetooth() {
  Serial.println("Setub bluettooth");
  RemoteXY_Init();
  RemoteXY.pidKp = pidKp;
  RemoteXY.pidKpEdit = pidKp;
  RemoteXY.pidKi = pidKi;
  RemoteXY.pidKiEdit = pidKi;
}

void setup() {
  Wire.begin();
  Wire.setClock(400000); // 400kHz I2C clock. Comment this line if having compilation difficulties
  Serial.begin(115200);
  setupMPU6050();
  setupWifi();
  setupOTA();
  // waitForOTA();
  setupMPWM(MOTOR_A1, MOTOR_A2, MOTOR_B1, MOTOR_B2);
  setupBluetooth();
  pid.SetMode(AUTOMATIC);
  pid.SetOutputLimits(-60, 60);
  Serial.println("setup done");
}

// ================================================================
// ===                    MAIN PROGRAM LOOP                     ===
// ================================================================


void processMPUData() {
  if (!dmpReady || !mpuInterrupt) return;
  mpuInterrupt = false;
  mpuIntStatus = mpu.getIntStatus();
  static unsigned long lastOutput = 0;
  if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) { // Get the Latest packet 
    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
    // inputAngle = ypr[2];
    inputAngle = ypr[2] * 180 / M_PI;
    pid.Compute();
    // double speed = constrain(pidOutput, -100.0, 100.0);
    speed = pidOutput;
    if (speed < 0) {
      speed = speed - 10;
    } else {
      speed = speed + 10;
    }
    if (enginesOn) {
      motorsGo(speed);
    }
    unsigned long now = millis();
    if (now - lastOutput > 250) {
      lastOutput = now;
      Serial.print("An: ");
      Serial.println(inputAngle);
      Serial.print("PID output: ");
      Serial.print(pidOutput);
      Serial.print(" speed ");
      Serial.println(speed);
    }
  }
}


void loop() {
  unsigned long loopStart = millis();
  ArduinoOTA.handle();
  RemoteXY_Handler();
  if (RemoteXY.butttonStop) {
    enginesOn = !enginesOn;
  }

  RemoteXY.ledState_g = enginesOn ? 255: 0;
  RemoteXY.ledState_r = !enginesOn ? 255: 0;
  RemoteXY.angle = inputAngle;
  RemoteXY.graph_var1 = speed;
  RemoteXY.graph_var2 = pidOutput;
  
  // Dabble.processInput();
  // if (Dabble.isAppConnected()) {
  //   uint16_t pot1Value = Inputs.getPot1Value();
  //   uint16_t pot2Value = Inputs.getPot2Value();
  //   onOff = Inputs.getSlideSwitch1Value();
  //   if (pot1Value != lastPot1Value) {
  //     lastPot1Value = pot1Value;
  //     pidKp = pot1Value / 100.0;
  //     Serial.print("Changing KP: "); Serial.println(pidKp); 
  //     pid.SetTunings(pidKp, pidKi, pidKd);
  //   }
  //   if (pot2Value != lastPot2Value) {
  //     lastPot2Value = pot2Value;
  //     pidKi = pot2Value / 100.0;
  //     Serial.print("Changing KI: "); Serial.println(pidKi); 
  //     pid.SetTunings(pidKp, pidKi, pidKd);
  //   }
  // }

  processMPUData();
  if (!enginesOn) {
    motorsStop();
  }
    
  loopSum += millis() - loopStart;
  loopCount++;
  if (loopSum >= 1000) {
    unsigned long average = loopSum / loopCount;
    Serial.print("Loops "); Serial.print(loopCount); Serial.print(" in "); Serial.println(loopSum);
    Serial.print("Average loop duration: "); 
    Serial.println(average);
    loopSum = 0;
    loopCount = 0;
  }
}

