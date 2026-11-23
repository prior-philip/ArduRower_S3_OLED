#include <Arduino.h>
#if ESP_ARDUINO_VERSION_MAJOR >= 4
#error "This sketch uses the BLEDevice API (esp32 core 2.x/3.x). Install esp32 core 3.3.x in Boards Manager - core 4.x has a new BLE API."
#endif
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <BLE2902.h>
#include "RunningAverage.h" // https://github.com/RobTillaart/Arduino/tree/master/libraries/RunningAverage

//#include "esp_bt_main.h"
//#include "esp_gap_bt_api.h"

#include <Wire.h>
#include <U8g2lib.h>
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_timer.h"

// ===========================================================================
//  ESP32-S3-Zero (Waveshare) + SH1107 128x128 I2C OLED  -  ArduRower port
//  Arduino IDE: board "ESP32S3 Dev Module" (official esp32 core by Espressif),
//               Tools > USB CDC On Boot: Enabled,  library: U8g2
// ===========================================================================

#define btnRIGHT  0
#define btnUP     1
#define btnDOWN   2
#define btnLEFT   3
#define btnSELECT 4
#define btnNONE   5

// Oled Display: SH1107 128x128 on I2C (same module/wiring as AirSuspension)
#define I2C_SDA_PIN   8
#define I2C_SCL_PIN   9
#define DISPLAY_ROTATION U8G2_R1   // AirSuspension uses R1; R0 / R2 / R3 rotate the screen
#define SCREEN_W 128
#define SCREEN_H 128
U8G2_SH1107_SEEED_128X128_F_HW_I2C u8g2(DISPLAY_ROTATION, U8X8_PIN_NONE);

// Deep sleep: after this much time without a sensor pulse the board goes to
// deep sleep and wakes again on the next reed-switch pulse on GPIO2.
#define IDLE_DETECT_MS 1000UL      // no pulse for this long = "idle": display refresh is reduced
#define IDLE_SLEEP_MS  300000UL    // deep sleep after this long without pulses (5 min), BLE connected or not

// CPU clock in MHz. 80 is the lowest the ESP32-S3 can run with BLE active (default would be 240).
#define CPU_MHZ 80

// Screen refresh intervals: while rowing only the CPU clock is reduced (normal refresh),
// once idle (no pulse for IDLE_DETECT_MS) the refresh is reduced to once per second.
#define DISPLAY_REFRESH_ROWING_MS 200
#define DISPLAY_REFRESH_IDLE_MS   1000

// Global Define 
#define _VERSION          0.05
#define BLE_SERVICE_NAME "WR S4BL3"           // name of the Bluetooth Service 

// Battery monitoring (optional). The S3-Zero has no battery sense of its own:
// wire VBAT -> 100k -> BATPIN -> 100k -> GND (1:1 divider) and set to 1.
#define ENABLE_BATTERY 0
#define BATPIN 1
#define BAT_MV_EMPTY 3200
#define BAT_MV_FULL  4200

#define SerialDebug Serial                   // Usb is used by S4, additionnal port on SAMD21
      
#define FitnessMachineService       0x1826
#define FitnessMachineControlPoint  0x2AD9    // Beta Implementation
#define FitnessMachineFeature       0x2ACC    // CX Not implemented yet
#define FitnessMachineStatus        0x2ADA    // Beta Implementation
#define FitnessMachineRowerData     0x2AD1    // CX Main cx implemented
#define DEVICE_INFORMATION          0x180A

#define batteryService              0x180F    // Additionnal battery service
#define batteryLevel                0x2A19    // Additionnal cx to battery service

BLEServer *pServer = NULL;
BLECharacteristic * pCtrCharacteristic;
BLECharacteristic * pDtCharacteristic;
BLECharacteristic * pFmfCharacteristic;
BLECharacteristic * pStCharacteristic;
BLECharacteristic * pBatCharacteristic;
BLEAdvertising *pAdvertising;

BLECharacteristic * pCharacteristic24;
BLECharacteristic * pCharacteristic25;
BLECharacteristic * pCharacteristic26;
BLECharacteristic * pCharacteristic27;
BLECharacteristic * pCharacteristic28;
BLECharacteristic * pCharacteristic29;

volatile bool deviceConnected = false;
bool oldDeviceConnected = false;
//uint32_t value = 0;
//uint8_t txValue = 0;

// Service
int32_t fitnessMachineServiceId;
int32_t batteryServiceId;

uint16_t  rowerDataFlagsP1=0b0000011111110;
uint16_t  rowerDataFlagsP2=0b1111100000001;

  //P1
  // 0000000000001 - 1   - 0x001 - More Data 0 <!> WARNINNG <!> This Bit is working the opposite way, 0 means field is present, 1 means not present
  // 0000000000010 - 2   - 0x002 - Average Stroke present
  // 0000000000100 - 4   - 0x004 - Total Distance Present
  // 0000000001000 - 8   - 0x008 - Instantaneous Pace present
  // 0000000010000 - 16  - 0x010 - Average Pace Present
  // 0000000100000 - 32  - 0x020 - Instantaneous Power present
  // 0000001000000 - 64  - 0x040 - Average Power present
  // 0000010000000 - 128 - 0x080 - Resistance Level present
  //P2
  // 0000100000000 - 256 - 0x080 - Expended Energy present
  // 0001000000000 - 512 - 0x080 - Heart Rate present
  // 0010000000000 - 1024- 0x080 - Metabolic Equivalent present
  // 0100000000000 - 2048- 0x080 - Elapsed Time present
  // 1000000000000 - 4096- 0x080 - Remaining Time present

  //  C1  Stroke Rate             uint8     Position    2 (After the Flag 2bytes)
  //  C1  Stroke Count            uint16    Position    3 
  //  C2  Average Stroke Rate     uint8     Position    5
  //  C3  Total Distance          uint24    Position    6
  //  C4  Instantaneous Pace      uint16    Position    9
  //  C5  Average Pace            uint16    Position    11
  //  C6  Instantaneous Power     sint16    Position    13
  //  C7  Average Power           sint16    Position    15
  //  C8  Resistance Level        sint16    Position    17
  //  C9  Total Energy            uint16    Position    19
  //  C9  Energy Per Hour         uint16    Position    21
  //  C9  Energy Per Minute       uint8     Position    23
  //  C10 Heart Rate              uint8     Position    24
  //  C11 Metabolic Equivalent    uint8     Position    25
  //  C12 Elapsed Time            uint16    Position    26
  //  C13 Remaining Time          uint16    Position    28

  // fitnessMachineControlPointId
  // OPCODE     DESCRIPTION                                             PARAMETERS
  // 0x01       Reset                                                   N/A
  // 0x02       Fitness Machine Stopped or Paused by the User
  // 0x03       Fitness Machine Stopped by Safety Key                   N/A
  // 0x04       Fitness Machine Started or Resumed by the User          N/A
  // 0x05       Target Speed Changed
  // 0x06       Target Incline Changed
  // 0x07       Target Resistance Level Changed
  // 0x08       Target Power Changed
  // 0x09       Target Heart Rate Changed
  // 0x0B       Targeted Number of Steps Changed                        New Targeted Number of Steps Value (UINT16, in Steps with a resolution of 1)
  // 0x0C       Targeted Number of Strides Changed                      New Targeted Number of Strides (UINT16, in Stride with a resolution of 1)
  // 0x0D       Targeted Distance Changed                               New Targeted Distance (UINT24, in Meters with a resolution of 1)
  // 0x0E       Targeted Training Time Changed                          New Targeted Training Time (UINT16, in Seconds with a resolution of 1)


struct rowerDataKpi{
  int bpm; // Start of Part 1
  int strokeCount;
  //int tmpstrokeRate;
  int strokeRate;
  int averageStokeRate;
  int totalDistance;
  int instantaneousPace;
  int tmpinstantaneousPace;
  int averagePace;
  int instantaneousPower;
  int averagePower;
  int resistanceLevel;
  int totalEnergy; // Start of Part 2
  int energyPerHour;
  int energyPerMinute;
  int heartRate;
  int metabolicEquivalent;
  int elapsedTime;
  int elapsedTimeSec;
  int elapsedTimeMin;
  int elapsedTimeHour;
  int remainingTime;
};

struct rowerDataKpi rdKpi;

bool bleInitFlag=false;
bool bleConnectionStatus=false;

unsigned long currentTime=0;
unsigned long previousTime=0;
unsigned long battPreviousTime=0;

/*-----------------SETUP--PINS-----------------------------*/
const int ROWERINPUT = 2; // the input pin where the waterrower sensor is connected
const int BUTTONSPIN = 0;   // BOOT button on the S3-Zero (active LOW)
volatile unsigned long lastActivityMs = 0;   // last sensor pulse, for idle deep sleep

/*-----------------CONSTANTS-------------------------------*/
//const float Ratio = 4.8; // from old script 4.8; meters per rpm = circumference of rotor (D=34cm) -> 1,068m -> Ratio = 0.936 ; WaterRower 7,3 m/St. -> Ratio: 3.156
//const float Ratio = 0.79; //one magnet

// ---- Sensor concept ------------------------------------------------------
// SENSOR_FLYWHEEL: magnet(s) on the paddle/flywheel shaft, turning one way (original ArduRower).
// SENSOR_PULLEY:   one magnet on the strap deflection pulley. The pulley turns forward on the
//                  drive and back on the recovery; strokes are detected from the pauses at the
//                  catch and the boat speed is estimated from the handle speed during the drive.
#define SENSOR_FLYWHEEL 0
#define SENSOR_PULLEY   1
#define SENSOR_MODE SENSOR_PULLEY

// -- pulley mode settings
#define PULLEY_DIAMETER_CM   11.0f  // strap runs over this pulley, 1 magnet = 1 pulse per turn
#define SPEED_FACTOR_K       2.8f   // boat speed [m/s] = K x handle speed during the drive. Calibrate:
                                    // K_new = K_old x (real distance / shown distance)
#define STROKE_GAP_MIN_MS    400    // a pulse gap must be at least this long ...
#define STROKE_GAP_PCT       160    // ... and this % of the median pulse interval to start a stroke
#define STROKE_LOCKOUT_PCT   70     // ignore gaps earlier than this % of the last stroke period (finish)
#define DRIVE_SPEED_INTERVALS 2     // pulse intervals at the start of the drive used for handle speed

// ---- Rotor magnets (flywheel mode) ---------------------------------------
// Number of magnets per flywheel/rotor revolution (= reed pulses per revolution).
// The original firmware was built for 4 magnets (Ratio 3.156 = 4 x 0.789 clicks per meter).
#define MAGNETS_PER_ROUND 4
#define CLICKS_PER_METER_PER_MAGNET 0.789f
const float Ratio = MAGNETS_PER_ROUND * CLICKS_PER_METER_PER_MAGNET;   // clicks per meter
// Stroke detection scales with the pulse rate: original values 20 (4 magnets) / 3 (1 magnet)
#define STROKE_ACCEL_THRESHOLD ((MAGNETS_PER_ROUND) == 1 ? 3 : 5 * (MAGNETS_PER_ROUND))
#define STROKE_PUFFER (5 * (MAGNETS_PER_ROUND))                     // clicks to ignore after a stroke

// ---- Raw pulse logging --------------------------------------------------
// 1 = print every raw reed edge (before debouncing) over USB serial as
//   E,<micros>,<level>      level 0 = contact closed (magnet at the switch), 1 = open
// to analyse the pulse pattern (e.g. magnet on the deflection pulley). Capture with the
// Arduino Serial Monitor (115200) or any terminal and save to a file.
#define PULSE_LOG 0
#define PULSE_LOG_SIZE 512                 // ring buffer entries (edges between two prints)

// ---- Simulation (demo mode) ----------------------------------------------
// Hold the BOOT button for 1 s on the "Waiting for start" screen to feed the normal
// processing chain (display + BLE to watch/app) with a simulated rower. Any button press stops it.
#define SIM_SPM         30      // simulated strokes per minute
#define SIM_V_BASE      3.4f    // m/s during the recovery
#define SIM_V_PEAK      1.6f    // extra m/s at the peak of the drive
#define SIM_DRIVE_FRAC  0.35f   // part of a stroke cycle that is the drive
volatile bool simActive = false;
void startSimulation();
void stopSimulation();

RunningAverage stm_RA(21); // size of array for strokes/min
RunningAverage mps_RA(7); // size of array for meters/second -> emulates momentum of boat
RunningAverage battery_RA(40);

/*-----------------VARIABLES-----------------------------*/
long start;
unsigned long movingMs = 0;   // elapsed time that only runs while rowing (paused when m/s or spm is zero)
float rounds;
volatile unsigned long click_time = 0;
volatile unsigned long last_click_time = 0;
volatile unsigned long old_split = 0;
volatile unsigned long split_time = 0;
volatile unsigned long start_split;
long debouncing_time = 15; //Debouncing Time in Milliseconds
volatile unsigned long last_micros;
volatile int clicks = 0;
volatile int clicks_old = 0;
volatile float data_output = 0;
volatile long rpm = 0;
volatile long old_rpm = 0;
volatile long stm = 0;
volatile int old_strokes = 0;
volatile float spm = 0;
int old_spm = 0;
volatile long stmra = 0;
int accel = 0;
int puffer = 0;
volatile float Ms = 0;
volatile int meters = 0;
volatile int meters_old = 0;
//volatile int trigger = 0;
unsigned long timer1 = 0;
unsigned long timer2 = 0;
int stage = 0; // sets case for what parameter to be displayed
int rotation = 0;
int strokes = 0;
int trend = 0;
int resetTimer = 0;


class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      SerialDebug.println("CONNECTED");
      resetTimer = 0;
    };
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      SerialDebug.println("DISCONNECTED");
      
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      auto rxValue = pCharacteristic->getValue();   // std::string (core 2.x) or String (core 3.x)
      if (rxValue.length() > 0) {
        SerialDebug.println("*********");
        SerialDebug.print("Received Value: ");
        {
          for (size_t i = 0; i < rxValue.length(); i++) {
            char chr = rxValue[i];
            SerialDebug.print(chr);
            SerialDebug.print(" Int:");
            SerialDebug.print((int)chr);
            SerialDebug.print(' ');
          }
        }  
          SerialDebug.println();
        SerialDebug.println("*********");
      }
    }
};

void initBLE(){
  // Initialise the module 
  SerialDebug.print(F("Init BLE:"));
  // Create the BLE Device
  BLEDevice::init(BLE_SERVICE_NAME);
  //BLEDevice::init("WAHOO BLUESC");
  
  // Create the BLE Server
  pServer = BLEDevice::createServer();   // global (was shadowed by a local, leaving the global NULL)
  pServer->setCallbacks(new MyServerCallbacks());
  
  // Add the Fitness Machine Service definition 
  // Service ID should be 1 
  SerialDebug.println(F("Adding the Fitness Machine Service definition (UUID = 0x1826): "));
  // Create the BLE Service
  BLEService *pService = pServer->createService(BLEUUID((uint16_t)FitnessMachineService));

  // Add the Fitness Machine Rower Data characteristic 
  // Chars ID for Measurement should be 1 
    pDtCharacteristic = pService->createCharacteristic(
          BLEUUID((uint16_t)FitnessMachineRowerData),
          BLECharacteristic::PROPERTY_NOTIFY
          );
    pDtCharacteristic->addDescriptor(new BLE2902());      
    pDtCharacteristic->setCallbacks(new MyCallbacks());

    pFmfCharacteristic = pService->createCharacteristic(
          BLEUUID((uint16_t)FitnessMachineFeature),
          BLECharacteristic::PROPERTY_READ
          );
    pFmfCharacteristic->addDescriptor(new BLE2902());

    // Add the Fitness Machine Control Point characteristic 
    pCtrCharacteristic = pService->createCharacteristic(
          BLEUUID((uint16_t)FitnessMachineControlPoint),
          BLECharacteristic::PROPERTY_WRITE 
          );
    pCtrCharacteristic->addDescriptor(new BLE2902());
    pCtrCharacteristic->setCallbacks(new MyCallbacks());

    pBatCharacteristic = pService->createCharacteristic(
          BLEUUID((uint16_t)batteryLevel),
          BLECharacteristic::PROPERTY_NOTIFY
          );
    pBatCharacteristic->addDescriptor(new BLE2902());

    // Add the Fitness Machine Status characteristic 
    pStCharacteristic = pService->createCharacteristic(
          BLEUUID((uint16_t)FitnessMachineStatus),
          BLECharacteristic::PROPERTY_NOTIFY
          );
    pStCharacteristic->addDescriptor(new BLE2902());

  // Start the service
  pService->start();

 
  //-------------------------------------------------------------------------------------

  BLEService *pService2 = pServer->createService(BLEUUID((uint16_t)DEVICE_INFORMATION));
  pCharacteristic24 = pService2->createCharacteristic(
    BLEUUID((uint16_t)0x2A24),
    BLECharacteristic::PROPERTY_READ
    );
  pCharacteristic25 = pService2->createCharacteristic(
    BLEUUID((uint16_t)0x2A25),
    BLECharacteristic::PROPERTY_READ
    );
  pCharacteristic26 = pService2->createCharacteristic(
    BLEUUID((uint16_t)0x2A26),
    BLECharacteristic::PROPERTY_READ
    );    
  pCharacteristic27 = pService2->createCharacteristic(
    BLEUUID((uint16_t)0x2A27),
    BLECharacteristic::PROPERTY_READ
    );
  pCharacteristic28 = pService2->createCharacteristic(
    BLEUUID((uint16_t)0x2A28),
    BLECharacteristic::PROPERTY_READ
    );
  pCharacteristic29 = pService2->createCharacteristic(
    BLEUUID((uint16_t)0x2A29),
    BLECharacteristic::PROPERTY_READ
    );            
  pService2->start();

  pCharacteristic24->setValue("4");
  pCharacteristic25->setValue("0000");
  pCharacteristic26->setValue("0.30");
  pCharacteristic27->setValue("2.2BLE");
  pCharacteristic28->setValue("4.3");
  pCharacteristic29->setValue("Waterrower");

  char cRower[8];
  cRower[0]=0x26;
  cRower[1]=0x56;
  cRower[2]=0x00;
  cRower[3]=0x00;
  cRower[4]=0x00;
  cRower[5]=0x00;
  cRower[6]=0x00;
  cRower[7]=0x00;
  pFmfCharacteristic->setValue((uint8_t* )cRower, 8);

//--------------------------------------------------------------------------------------------------------

  // Start advertising
  SerialDebug.println(F("Adding Fitness Machine Service UUID to the advertising payload "));
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  BLEAdvertisementData oAdvertisementData;
  {
    static const char advRaw[] = { 0x02, 0x01, 0x06, 0x05, 0x02, 0x26, 0x18, 0x0a, 0x18 };
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    oAdvertisementData.addData(String(advRaw, sizeof(advRaw)));
#else
    oAdvertisementData.addData(std::string(advRaw, sizeof(advRaw)));
#endif
  }
  //oAdvertisementData.addData(std::string{ 0x01, 0xFC});
  pAdvertising->setScanResponse(true);
  pAdvertising->setAdvertisementData(oAdvertisementData);

  //pAdvertising->addServiceUUID(BLEUUID((uint16_t)GATT_CSC_UUID));
  pAdvertising->addServiceUUID(BLEUUID((uint16_t)FitnessMachineService));
  //pAdvertising->setAppearance(833);
  //pAdvertising->setAppearance(1154); //BLE_APPEARANCE_CYCLING_SPEED_SENSOR
  BLEDevice::startAdvertising();

  bleInitFlag=true;
  SerialDebug.println();
}

void initBleData(){
  rdKpi.bpm=0;
  rdKpi.strokeCount=0;
  //rdKpi.tmpstrokeRate=0;
  rdKpi.strokeRate=0;
  rdKpi.averageStokeRate=0;
  rdKpi.totalDistance=0;
  rdKpi.tmpinstantaneousPace=0;
  rdKpi.instantaneousPace=0;
  rdKpi.averagePace=0;
  rdKpi.instantaneousPower=0;
  rdKpi.averagePower=0;
  rdKpi.resistanceLevel=0;
  rdKpi.totalEnergy=0;
  rdKpi.energyPerHour=0;
  rdKpi.energyPerMinute=0;
  rdKpi.heartRate=0;
  rdKpi.metabolicEquivalent=0;
  rdKpi.elapsedTime=0;
  rdKpi.elapsedTimeSec=0;
  rdKpi.elapsedTimeMin=0;
  rdKpi.elapsedTimeHour=0;
  rdKpi.remainingTime=0;
}

void setCxLightRowerData(){
#ifdef DEEPTRACE
  SerialDebug.printf("sendBleLightData() start");
#endif
  // This function is a subset of field to be sent in one piece
  // An alternative to the sendBleData()
  uint16_t  rowerDataFlags=0b0000001111110; //0x7E
  
  // 0000000000001 - 1   - 0x001 + More Data 0 <!> WARNINNG <!> This Bit is working the opposite way, 0 means field is present, 1 means not present
  // 0000000000010 - 2   - 0x002 + Average Stroke present
  // 0000000000100 - 4   - 0x004 + Total Distance Present
  // 0000000001000 - 8   - 0x008 + Instantaneous Pace present
  // 0000000010000 - 16  - 0x010 + Average Pace Present
  // 0000000100000 - 32  - 0x020 + Instantaneous Power present
  // 0000001000000 - 64  - 0x040 + Average Power present
  // 0000010000000 - 128 - 0x080 - Resistance Level present
  // 0000100000000 - 256 - 0x080 + Expended Energy present
  // 0001000000000 - 512 - 0x080 - Heart Rate present
  // 0010000000000 - 1024- 0x080 - Metabolic Equivalent present
  // 0100000000000 - 2048- 0x080 - Elapsed Time present
  // 1000000000000 - 4096- 0x080 - Remaining Time present

  //  C1  Stroke Rate             uint8     Position    2  + (After the Flag 2bytes)
  //  C1  Stroke Count            uint16    Position    3  +
  //  C2  Average Stroke Rate     uint8     Position    5  +
  //  C3  Total Distance          uint24    Position    6  +
  //  C4  Instantaneous Pace      uint16    Position    9  +
  //  C5  Average Pace            uint16    Position    11 +
  //  C6  Instantaneous Power     sint16    Position    13 +
  //  C7  Average Power           sint16    Position    15 +
  //  C8  Resistance Level        sint16    Position    17 -
  //  C9  Total Energy            uint16    Position    19 +
  //  C9  Energy Per Hour         uint16    Position    21 +
  //  C9  Energy Per Minute       uint8     Position    23 +
  //  C10 Heart Rate              uint8     Position    24 -
  //  C11 Metabolic Equivalent    uint8     Position    25 -
  //  C12 Elapsed Time            uint16    Position    26 -
  //  C13 Remaining Time          uint16    Position    28 -

  char cRower[17];

  cRower[0]=rowerDataFlags & 0x000000FF;
  cRower[1]=(rowerDataFlags & 0x0000FF00) >> 8;
  
  //rdKpi.strokeRate=rdKpi.tmpstrokeRate*2;
  cRower[2] = rdKpi.strokeRate & 0x000000FF;
  
  cRower[3] = rdKpi.strokeCount & 0x000000FF;
  cRower[4] = (rdKpi.strokeCount & 0x0000FF00) >> 8;
  
  cRower[5] = rdKpi.averageStokeRate & 0x000000FF;
  
  cRower[6] = rdKpi.totalDistance &  0x000000FF;
  cRower[7] = (rdKpi.totalDistance & 0x0000FF00) >> 8;
  cRower[8] = (rdKpi.totalDistance & 0x00FF0000) >> 16;
  
  if (rdKpi.tmpinstantaneousPace>0) // Avoid Divide by Zero
    rdKpi.instantaneousPace=(100000/rdKpi.tmpinstantaneousPace)/2;
  cRower[9] = rdKpi.instantaneousPace & 0x000000FF;
  cRower[10] = (rdKpi.instantaneousPace & 0x0000FF00) >> 8;
  
  cRower[11] = rdKpi.averagePace & 0x000000FF;
  cRower[12] = (rdKpi.averagePace & 0x0000FF00) >> 8;

  cRower[13] = rdKpi.instantaneousPower & 0x000000FF;
  cRower[14] = (rdKpi.instantaneousPower & 0x0000FF00) >> 8;

  cRower[15] = rdKpi.averagePower & 0x000000FF;
  cRower[16] = (rdKpi.averagePower & 0x0000FF00) >> 8;

  //gatt.setChar(fitnessMachineRowerDataId, cRower, 17);
  pDtCharacteristic->setValue((uint8_t* )cRower, 17);
  pDtCharacteristic->notify();


#ifdef DEEPTRACE
  SerialDebug.printf("sendBleLightData() end");
#endif
}

void setCxRowerData(){
  // Due the size limitation of the message in the BLE Stack of the NRF
  // the message will be split in 2 parts with the according Bitfield (read the spec :) )
  // rowerDataFlagsP1=0b0000011111110 = 0xFE
  // rowerDataFlagsP2=0b1111100000001 = 0x1F01
 
  char cRower[19]; // P1 is the biggest part whereas P2 is 13
  
  // Send the P1 part of the Message
  cRower[0]=rowerDataFlagsP1 & 0x000000FF;
  cRower[1]=(rowerDataFlagsP1 & 0x0000FF00) >> 8;
  
  //rdKpi.strokeRate=rdKpi.tmpstrokeRate*2;
  cRower[2] = rdKpi.strokeRate & 0x000000FF;
  
  cRower[3] = rdKpi.strokeCount & 0x000000FF;
  cRower[4] = (rdKpi.strokeCount & 0x0000FF00) >> 8;
  
  cRower[5] = rdKpi.averageStokeRate & 0x000000FF;
  
  cRower[6] = rdKpi.totalDistance &  0x000000FF;
  cRower[7] = (rdKpi.totalDistance & 0x0000FF00) >> 8;
  cRower[8] = (rdKpi.totalDistance & 0x00FF0000) >> 16;
  
  cRower[9] = rdKpi.instantaneousPace & 0x000000FF;
  cRower[10] = (rdKpi.instantaneousPace & 0x0000FF00) >> 8;
  
  cRower[11] = rdKpi.averagePace & 0x000000FF;
  cRower[12] = (rdKpi.averagePace & 0x0000FF00) >> 8;

  cRower[13] = rdKpi.instantaneousPower & 0x000000FF;
  cRower[14] = (rdKpi.instantaneousPower & 0x0000FF00) >> 8;

  cRower[15] = rdKpi.averagePower & 0x000000FF;
  cRower[16] = (rdKpi.averagePower & 0x0000FF00) >> 8;

  cRower[17] = rdKpi.resistanceLevel & 0x000000FF;
  cRower[18] = (rdKpi.resistanceLevel & 0x0000FF00) >> 8;

  pDtCharacteristic->setValue((uint8_t* )cRower, 19);
  pDtCharacteristic->notify();

  // Send the P2 part of the Message
  cRower[0]=rowerDataFlagsP2 & 0x000000FF;
  cRower[1]=(rowerDataFlagsP2 & 0x0000FF00) >> 8;
         
  cRower[2] = rdKpi.totalEnergy & 0x000000FF;
  cRower[3] = (rdKpi.totalEnergy & 0x0000FF00) >> 8;

  cRower[4] = rdKpi.energyPerHour & 0x000000FF;
  cRower[5] = (rdKpi.energyPerHour & 0x0000FF00) >> 8;

  cRower[6] = rdKpi.energyPerMinute & 0x000000FF;

  cRower[7] = rdKpi.bpm & 0x000000FF;

  cRower[8] = rdKpi.metabolicEquivalent& 0x000000FF;

  //rdKpi.elapsedTime=rdKpi.elapsedTimeSec+rdKpi.elapsedTimeMin*60+rdKpi.elapsedTimeHour*3600;
  cRower[9] =   rdKpi.elapsedTime & 0x000000FF;
  cRower[10] = (rdKpi.elapsedTime & 0x0000FF00) >> 8;

  cRower[11] = rdKpi.remainingTime & 0x000000FF;
  cRower[12] = (rdKpi.remainingTime & 0x0000FF00) >> 8;
  
  pDtCharacteristic->setValue((uint8_t* )cRower, 13);
  pDtCharacteristic->notify();

}

// FTMS pace: seconds per 500 m (uint16). Standing still -> 65535 (what the original firmware
// sent in that case, so existing apps keep working).
int paceSecPer500(float speedMs) {
  if (speedMs < 0.0077f) return 65535;
  float p = 500.0f / speedMs;
  return (p > 65535.0f) ? 65535 : (int)round(p);
}

void setCxBattery(){
  #ifdef DEEPTRACE
    SerialDebug.printf("sendBleBattery() start");
  #endif
  char hexBat[2];
  int batteryLevelPercent = round(battery_RA.getAverage() * 100 / 127);
  /*
  float measuredVbat = analogRead(VBATPIN);
  measuredVbat *= 2;    // we divided by 2, so multiply back
  measuredVbat *= 3.3;  // Multiply by 3.3V, our reference voltage
  measuredVbat /= 1024; // convert to voltage

  batteryLevelPercent=((measuredVbat-2.9)/(5.15-2.90))*100;

  SerialDebug.print("Mesured Bat:");
  SerialDebug.println(batteryLevelPercent);
  */

  hexBat[0]=batteryLevelPercent & 0x000000FF;
  hexBat[1]='\0'; // just in case 

  pBatCharacteristic->setValue((uint8_t* )hexBat, 1);
  pBatCharacteristic->notify();

  #ifdef DEEPTRACE
    SerialDebug.printf("sendBleBattery() end");
  #endif
}

int readBattery127() {   // 0..127, same scale the original battery bar used
#if ENABLE_BATTERY
  int mv = analogReadMilliVolts(BATPIN) * 2;   // 1:1 divider
  return constrain(map(mv, BAT_MV_EMPTY, BAT_MV_FULL, 0, 127), 0, 127);
#else
  return 127;
#endif
}

// Draws one centred line of text (baseline y)
void drawCentered(int y, const char *txt) {
  u8g2.drawStr((SCREEN_W - u8g2.getStrWidth(txt)) / 2, y, txt);
}

void drawRight(int xRight, int y, const char *txt) {
  u8g2.drawStr(xRight - u8g2.getStrWidth(txt), y, txt);
}

void drawWaitingScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB18_tf);
  drawCentered(60, "Waiting");
  drawCentered(84, "for start");
  u8g2.setFont(u8g2_font_helvB08_tr);
  drawCentered(108, "hold BOOT 1s: demo");
  drawCentered(121, "sleeps when idle");
  u8g2.sendBuffer();
}

// true when there were no sensor pulses for IDLE_DETECT_MS
bool rowerIdle() {
  return (millis() - lastActivityMs) > IDLE_DETECT_MS;
}

// true when there were no sensor pulses for IDLE_SLEEP_MS (independent of a BLE connection;
// going to sleep drops a connected watch/app, it reconnects when rowing resumes)
bool idleTooLong() {
  return (millis() - lastActivityMs) > IDLE_SLEEP_MS;
}

// Put the board into deep sleep. Wakes on the next reed-switch pulse
// (GPIO2 pulled LOW), then runs setup() again from the start.
void goToSleep() {
  SerialDebug.println("Idle - going to deep sleep, wake on GPIO2 pulse");
  SerialDebug.flush();
  detachInterrupt(ROWERINPUT);

  u8g2.clearBuffer();
  u8g2.sendBuffer();
  u8g2.setPowerSave(1);          // display off

  // The reed switch must be open (pin HIGH) first, otherwise we would wake instantly
  unsigned long released = millis();
  while (millis() - released < 100) {
    if (digitalRead(ROWERINPUT) == LOW) released = millis();
    delay(5);
  }

  esp_sleep_enable_ext0_wakeup((gpio_num_t)ROWERINPUT, 0);   // wake when pin goes LOW
  rtc_gpio_pullup_en((gpio_num_t)ROWERINPUT);                // keep the pull-up during sleep
  rtc_gpio_pulldown_dis((gpio_num_t)ROWERINPUT);
  esp_deep_sleep_start();
}

void row_start() {
  drawWaitingScreen();
  delay(100);
  lastActivityMs = millis();
  unsigned long pressStart = 0;
  while (clicks == 0) {
    pulseLogFlush();
    if (idleTooLong()) goToSleep();
    if (digitalRead(BUTTONSPIN) == LOW) {          // long press BOOT -> demo mode
      if (pressStart == 0) pressStart = millis();
      else if (!simActive && millis() - pressStart > 1000) {
        startSimulation();
        while (digitalRead(BUTTONSPIN) == LOW) delay(10);   // wait for release, it would stop the demo
        delay(50);
      }
    } else {
      pressStart = 0;
    }
    delay(10);
  }

  reset();
  rowing();
  if (simActive) stopSimulation();                 // button press / disconnect ends the demo
}

void variableParameter(){
    //lcd.setCursor(9, 0);
    switch (stage) {
    case 1:
      //lcd.print("Rot");
      data_output = clicks;
      break;
    case 2:
      //lcd.print("Rpm");
      data_output = rpm;
    break;
    case 3:
        //lcd.print("Trend ");
        if (trend == 0){
          //lcd.print("c");
        }
        if (trend == 1){
          //lcd.print("-");
        }
        if (trend == 2){
          //lcd.print("+");
        }
      data_output = 1;
    break;
    case 4:
      //lcd.print("St ");
      data_output = strokes;
    break;
    case 5:
      //lcd.print("StA");
      data_output = stm;
    break;
    case 6:
      //lcd.print("StM");
      data_output = stmra;
    break;
    default:
       //lcd.print("Sp5");
       data_output = split_time;
    break;
    }
    if(data_output >= 10) {
      if(data_output >= 100) {
      //lcd.print(" ");
      } else {
        //lcd.print("  ");
      }
    } else {
    //lcd.print("   ");
    }
    //lcd.print(data_output);
}

/*------------------INTERRUPT-----------------------------------*/
void IRAM_ATTR calcrpm() {
  click_time = millis();
  rpm = 60000 / (click_time - last_click_time);
  last_click_time = click_time;
  accel = rpm - old_rpm;
  if ((accel > STROKE_ACCEL_THRESHOLD) && (puffer == 0)){ // threshold eliminates micro "acceleration" due to splashing water (20 for 4 magnets, 3 for one)
    strokes ++;
    puffer = STROKE_PUFFER; // prevents counting two strokes due to still accelerating rotor
  }
  if (puffer > 0){
    puffer --;
  }
  old_rpm = rpm;
}

#if SENSOR_MODE == SENSOR_PULLEY
// ---- pulley mode stroke detection (integer only: runs in the GPIO interrupt) ----
volatile uint32_t pLastPulseMs = 0;
volatile uint32_t pLastStrokeMs = 0;
volatile uint32_t pDriveStartMs = 0;
volatile int8_t   pDriveCount = -1;          // intervals measured in the current drive, -1 = idle
volatile uint32_t pLastPeriod = 0;           // ms between the last two strokes, 0 = none
volatile uint32_t pPeriods[3];               // last stroke periods
volatile uint8_t  pPeriodIdx = 0, pPeriodCnt = 0;
volatile uint32_t pDriveMs[3];               // last drive measurement times
volatile uint8_t  pDriveIdx = 0, pDriveCnt = 0;
volatile uint32_t pIv[8];                    // recent pulse intervals (without gaps)
volatile uint8_t  pIvIdx = 0, pIvCnt = 0;

float pulleySpm = 0;                         // updated 1x/s in the main loop
float pulleyMs = 0;
float metersF = 0;

static uint32_t IRAM_ATTR pulleyMedianIv() {
  uint32_t a[8];
  uint8_t n = pIvCnt;
  for (uint8_t i = 0; i < n; i++) {          // insertion sort, max 8 values
    uint32_t v = pIv[i];
    int8_t j = i - 1;
    while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
    a[j + 1] = v;
  }
  return a[n / 2];
}

void IRAM_ATTR pulleyPulse() {
  uint32_t t = millis();
  uint32_t iv = t - pLastPulseMs;
  pLastPulseMs = t;
  uint32_t med = (pIvCnt >= 4) ? pulleyMedianIv() : 300;
  uint32_t since = t - pLastStrokeMs;
  uint32_t lock = (pLastPeriod > 0) ? pLastPeriod * STROKE_LOCKOUT_PCT / 100 : 1000;
  bool gap = (iv >= STROKE_GAP_MIN_MS) && (iv * 100 > med * STROKE_GAP_PCT);

  if (gap && since >= lock) {                // catch -> new stroke, drive starts with this pulse
    if (pLastStrokeMs != 0 && since < 6000) {
      pLastPeriod = since;
      pPeriods[pPeriodIdx] = since;
      pPeriodIdx = (pPeriodIdx + 1) % 3;
      if (pPeriodCnt < 3) pPeriodCnt++;
    } else {
      pLastPeriod = 0;                       // first stroke after a pause
    }
    pLastStrokeMs = t;
    strokes++;
    pDriveStartMs = t;
    pDriveCount = 0;
  } else if (gap) {                          // finish (handle reverses): drive is over
    pDriveCount = -1;
  } else {
    pIv[pIvIdx] = iv;
    pIvIdx = (pIvIdx + 1) % 8;
    if (pIvCnt < 8) pIvCnt++;
    if (pDriveCount >= 0 && ++pDriveCount == DRIVE_SPEED_INTERVALS) {
      pDriveMs[pDriveIdx] = t - pDriveStartMs;
      pDriveIdx = (pDriveIdx + 1) % 3;
      if (pDriveCnt < 3) pDriveCnt++;
      pDriveCount = -1;
    }
  }
}

// 1x per second: stroke rate and estimated boat speed from the last 3 strokes
void pulleyUpdate() {
  const float circ = PULLEY_DIAMETER_CM * PI / 100.0f;   // m of strap per pulse
  uint32_t now = millis();
  uint32_t per = 0, drv = 0;
  uint8_t np = pPeriodCnt, nd = pDriveCnt;
  for (uint8_t i = 0; i < np; i++) per += pPeriods[i];
  for (uint8_t i = 0; i < nd; i++) drv += pDriveMs[i];
  if (np) per /= np;
  if (nd) drv /= nd;
  uint32_t since = now - pLastStrokeMs;
  uint32_t timeout = (per > 0) ? max((uint32_t)3000, 2 * per) : 3000;

  if (pLastStrokeMs != 0 && since < timeout) {
    pulleySpm = (per > 0) ? 60000.0f / per : 0;
    pulleyMs = (drv > 0) ? SPEED_FACTOR_K * circ * DRIVE_SPEED_INTERVALS * 1000.0f / drv : 0;
  } else {                                   // stopped: forget the history
    pulleySpm = 0;
    pulleyMs = 0;
    pPeriodCnt = 0;
    pDriveCnt = 0;
    pLastPeriod = 0;
  }
}

void pulleyReset() {
  pLastStrokeMs = 0;
  pLastPeriod = 0;
  pPeriodCnt = 0;
  pDriveCnt = 0;
  pIvCnt = 0;
  pDriveCount = -1;
  pulleySpm = 0;
  pulleyMs = 0;
  metersF = 0;
  if (pLastPulseMs != 0 && millis() - pLastPulseMs < 1000) {   // the pulse that started the session
    pLastStrokeMs = pLastPulseMs;                                // is the first drive
    strokes = 1;
    pDriveStartMs = pLastPulseMs;
    pDriveCount = 0;
  }
}
#endif

void IRAM_ATTR rowerinterrupt() {
  lastActivityMs = millis();
  /*  if (trigger == 0) {
      reset();
      trigger = 1;
    }*/
  if (rotation == 0){ // prevents double-count due to switch activating twice on the magnet passing once
    clicks++;
    rotation = 1;
#if SENSOR_MODE == SENSOR_PULLEY
    pulleyPulse();
#else
    calcrpm();
#endif
  } else {
    rotation = 0;
  }
}
/*------------------INTERRUPT END-----------------------------------*/


/*-----debounce------*/
#if PULSE_LOG
volatile uint32_t plogT[PULSE_LOG_SIZE];
volatile uint8_t  plogL[PULSE_LOG_SIZE];
volatile uint16_t plogHead = 0, plogTail = 0;
volatile uint32_t plogLost = 0;

static inline void IRAM_ATTR pulseLogAdd() {
  uint16_t next = (plogHead + 1) % PULSE_LOG_SIZE;
  if (next == plogTail) { plogLost++; return; }   // buffer full
  plogT[plogHead] = micros();
  plogL[plogHead] = digitalRead(ROWERINPUT);
  plogHead = next;
}

// prints buffered edges, call often from the main loops
void pulseLogFlush() {
  static bool header = false;
  if (!header) {
    SerialDebug.println("# ArduRower raw pulse log: E,<micros>,<level 0=closed 1=open>");
    header = true;
  }
  if (plogLost) {
    SerialDebug.printf("# lost %lu edges\n", (unsigned long)plogLost);
    plogLost = 0;
  }
  while (plogTail != plogHead) {
    SerialDebug.printf("E,%lu,%u\n", (unsigned long)plogT[plogTail], (unsigned)plogL[plogTail]);
    plogTail = (plogTail + 1) % PULSE_LOG_SIZE;
  }
}
#else
void pulseLogFlush() {}
#endif

void IRAM_ATTR rowerdebounceinterrupt() {
#if PULSE_LOG
  pulseLogAdd();
#endif
  if ((long)(micros() - last_micros) >= debouncing_time * 1000) {
    if (digitalRead(ROWERINPUT) == LOW) rotation = 0;   // stay in sync: count on the closing edge
    rowerinterrupt();
    last_micros = micros();
  }
}

/*-----simulation (demo mode)------*/
static esp_timer_handle_t simTimer = nullptr;
static float simT = 0;          // position in the stroke cycle [s]
static float simAcc = 0;        // fractional clicks
static int64_t simLastUs = 0;

// runs every 1 ms: speed profile of a rower (drive = speed up, recovery = constant), turned into reed pulses
static inline void simPulse() {
  rowerinterrupt();                                        // closing edge  -> counts a pulse
  rowerinterrupt();                                        // opening edge  -> ignored
}

static void simTick(void *) {
  int64_t now = esp_timer_get_time();
  float dt = (now - simLastUs) * 1e-6f;
  simLastUs = now;
#if SENSOR_MODE == SENSOR_PULLEY
  // pulse pattern recorded on the pulley (one 2.335 s stroke): 3 drive pulses, finish pause,
  // 3 recovery pulses, catch pause - scaled to SIM_SPM
  static const uint16_t off[6] = { 0, 290, 535, 985, 1315, 1585 };
  const float per = 60.0f / SIM_SPM;
  const float scale = per / 2.335f;
  float t0 = simT, t1 = simT + dt;
  for (int k = 0; k < 6; k++) {
    float o = off[k] * scale / 1000.0f;
    if (t0 < o && o <= t1) simPulse();
  }
  if (t1 >= per) {
    t1 -= per;
    simPulse();                                            // offset 0 of the next stroke
    for (int k = 1; k < 6; k++) {
      if (off[k] * scale / 1000.0f <= t1) simPulse();
    }
  }
  simT = t1;
  return;
#endif
  const float period = 60.0f / SIM_SPM;
  const float drive = SIM_DRIVE_FRAC * period;
  simT += dt;
  if (simT >= period) simT -= period;
  float v = SIM_V_BASE;                                    // m/s
  if (simT < drive) {
    float sn = sinf(PI * simT / drive);
    v += SIM_V_PEAK * sn * sn;
  }
  simAcc += v * Ratio * dt;                                // clicks = meters * clicks/meter
  while (simAcc >= 1.0f) {
    simAcc -= 1.0f;
    rowerinterrupt();                                      // closing edge  -> counts a click
    rowerinterrupt();                                      // opening edge  -> ignored (as with the real switch)
  }
}

void startSimulation() {
  SerialDebug.println("Simulation ON: " + String(SIM_SPM) + " spm");
  detachInterrupt(ROWERINPUT);
  rotation = 0;
  simT = 0;
  simAcc = 0;
  simLastUs = esp_timer_get_time();
  if (!simTimer) {
    esp_timer_create_args_t args = {};
    args.callback = &simTick;
    args.name = "sim";
    esp_timer_create(&args, &simTimer);
  }
  esp_timer_start_periodic(simTimer, 1000);
  simActive = true;
}

void stopSimulation() {
  if (!simActive) return;
  esp_timer_stop(simTimer);
  simActive = false;
  clicks = 0;                                              // back to "Waiting for start"
  attachInterrupt(ROWERINPUT, rowerdebounceinterrupt, CHANGE);
  SerialDebug.println("Simulation OFF");
}

//calculate and return meters per second
float calcmetersmin() {
#if SENSOR_MODE == SENSOR_PULLEY
  static unsigned long lastCalc = 0;
  unsigned long nowMs = millis();
  float dt = (lastCalc == 0) ? 1.0f : (nowMs - lastCalc) / 1000.0f;
  lastCalc = nowMs;
  pulleyUpdate();
  metersF += pulleyMs * dt;
  meters = (int)metersF;
  stm = (strokes * 60000) / (millis() - start);
  return pulleyMs;
#endif
  meters = clicks / Ratio;
  stm = (strokes*60000)/(millis()-start);
  mps_RA.addValue((clicks - clicks_old) / Ratio);
  //float x = (clicks - clicks_old) / Ratio;
  float x = mps_RA.getAverage(); // use of floating average emulates the momentum of a boat
  clicks_old = clicks;
  return x; 
}

//calculate strokes/minute as rolling average
void calcstmra() {
  stm_RA.addValue(strokes-old_strokes);
  old_strokes = strokes;
  stmra = stm_RA.getAverage()*60;
  if (stmra > stm){
    trend = 2;
  }
  if (stmra < stm){
    trend = 1;
  }
  if (stmra == stm){
    trend = 0;
  }
}

//calculate 500m split time (in seconds)
void split() {
  start_split = millis();
  split_time = (start_split - old_split) / 1000;
  old_split = millis();
}

//reset function (set to select button)
void reset() {
  clicks = 0;
  clicks_old = 0;
  meters = 0;
  meters_old = 0;
  click_time = 0;
  old_split = 0;
  split_time = 0;
  Ms = 0;
  timer1 = 0;
  timer2 = 0;
  start = millis();
  movingMs = 0;
  old_split = millis();
  strokes = 0;
  old_strokes = 0;
  stmra = 0;
  stm = 0;
  rpm = 0;
  stm_RA.clear();
  mps_RA.clear();
#if SENSOR_MODE == SENSOR_PULLEY
  pulleyReset();
#endif
  trend = 0;
  resetTimer = 0;
  //lcd.clear();
}

int read_LCD_buttons()
{
  bool key_in;
  key_in = digitalRead(BUTTONSPIN);
  delay(20); //switch debounce delay. Increase this delay if incorrect switch selections are returned.
  if ((!digitalRead(BUTTONSPIN)) && (!key_in)) 
    return btnLEFT;
  else
    return btnNONE;

  /*
  int adc_key_in = 0;
  adc_key_in = analogRead(BUTTONSPIN);      // read the value from the sensor
  delay(20); //switch debounce delay. Increase this delay if incorrect switch selections are returned.
  int k = (analogRead(BUTTONSPIN) - adc_key_in); //gives the button a slight range to allow for a little contact resistance noise
  if (5 < abs(k)) return btnNONE;  // double checks the keypress. If the two readings are not equal +/-k value after debounce delay, it tries again.
  // my buttons when read are centered at these valies: 0, 144, 329, 504, 741
  // we add approx 50 to those values and check to see if we are close
  if (adc_key_in > 1000) return btnNONE; // We make this the 1st option for speed reasons since it will be the most likely result
  if (adc_key_in < 50)   return btnRIGHT;
  if (adc_key_in < 195)  return btnUP;
  if (adc_key_in < 380)  return btnDOWN;
  if (adc_key_in < 555)  return btnLEFT;
  if (adc_key_in < 790)  return btnSELECT;
  return btnNONE;  // when all others fail, return this...
  */
}

// Timer pause: elapsed time only runs while the boat moves and strokes are counted
// (m/s and strokes/min both non-zero). Call regularly from the rowing loop.
void updateMovingTime() {
  static unsigned long lastMs = 0;
  unsigned long now = millis();
  if (lastMs != 0 && Ms >= 0.05f && spm >= 1.0f) movingMs += now - lastMs;   // NaN compares false -> paused
  lastMs = now;
}

// Elapsed time as m:ss (or h:mm once past an hour), as in the original
void formatElapsed(char *out, size_t n) {
  unsigned long t = movingMs / 1000UL;
  unsigned h = t / 3600UL, m = (t % 3600UL) / 60UL, sec = t % 60UL;
  if (h > 0) snprintf(out, n, "%u:%02u", h, m);
  else       snprintf(out, n, "%02u:%02u", m, sec);
}

// 128x128 layout (rows are right-aligned values with a small label on the left)
//   y   0..  3  battery bar (only if ENABLE_BATTERY)
//   y   9.. 41  elapsed time (large, centred)
//   y  44.. 68  meters
//   y  72.. 96  m/s
//   y 100..124  strokes/min
void drawRowingScreen() {
  char buf[16];

  u8g2.clearBuffer();

  if (simActive) {                       // demo mode marker
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(0, 11, "SIM");
  }

#if ENABLE_BATTERY
  int bat = constrain((int)battery_RA.getAverage(), 0, 127);
  u8g2.drawFrame(0, 0, SCREEN_W, 4);
  u8g2.drawBox(0, 0, bat + 1, 4);
#endif

  // elapsed time
  u8g2.setFont(u8g2_font_logisoso32_tn);
  formatElapsed(buf, sizeof(buf));
  drawCentered(41, buf);

  // labels
  u8g2.setFont(u8g2_font_helvB08_tr);
  u8g2.drawStr(2, 68, "meters");
  u8g2.drawStr(2, 96, "m/s");
  u8g2.drawStr(2, 124, "st/min");

  // values
  u8g2.setFont(u8g2_font_logisoso24_tn);
  snprintf(buf, sizeof(buf), "%d", (int)meters);
  drawRight(SCREEN_W - 2, 68, buf);
  snprintf(buf, sizeof(buf), "%.2f", (double)Ms);
  drawRight(SCREEN_W - 2, 96, buf);
  snprintf(buf, sizeof(buf), "%d", (int)round((spm + old_spm) / 2));
  drawRight(SCREEN_W - 2, 124, buf);

  u8g2.sendBuffer();
}

void setup() {

  setCpuFrequencyMhz(CPU_MHZ);   // save battery: nothing here needs more than 80 MHz

  bool wokeFromSleep = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0);

  // Start the OLED Display
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);
  u8g2.begin();
  u8g2.setPowerSave(0);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB18_tf);
  drawCentered(70, "ArduRower");
  u8g2.sendBuffer();

  SerialDebug.begin(115200);
  SerialDebug.println("/************************************");
  SerialDebug.println(" * DIY_Rower BLE Module ");
  SerialDebug.print  (" * Version ");
  SerialDebug.println(_VERSION);
  SerialDebug.println(" ***********************************/");

  /*
  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  if (esp_bt_controller_init(&bt_cfg) != ESP_OK)
    SerialDebug.println( "Bluetooth controller initialize failed");
  else  
    SerialDebug.println( "BT init succeess" );
  esp_bt_controller_enable(ESP_BT_MODE_BLE);
  */
  
  initBLE();
  currentTime=millis();
  previousTime=millis();
  initBleData();

  stm_RA.clear(); // explicitly start clean
  mps_RA.clear(); // explicitly start clean
  battery_RA.clear();
  rtc_gpio_deinit((gpio_num_t)ROWERINPUT);   // release the pin from deep-sleep (RTC) mode
  pinMode(ROWERINPUT, INPUT_PULLUP);
  pinMode(BUTTONSPIN, INPUT_PULLUP);
  int battery = readBattery127();
  for (int i = 1; i < battery_RA.getSize(); i++) {
     battery_RA.addValue(battery);
  }

  //digitalWrite(ROWERINPUT, HIGH);
  if (!wokeFromSleep) delay(500);   // keep the splash visible on a cold start
  lastActivityMs = millis();
  attachInterrupt(ROWERINPUT, rowerdebounceinterrupt, CHANGE);
  timer1 = millis();
  timer2 = millis();
  reset();
}

void rowing() {
  while (read_LCD_buttons() != btnLEFT) {
    // prints the variable parameter as selected by keypad
    // variableParameter();

    /*calculate split times every 500m*/
    if ((meters % 500) == 0 && meters > 0 ) {
      timer2 = millis();
      if ((millis() - timer2) >= 1000) {
        split();
      }
    }
    
    if ((millis() - timer1) >= 1000) {
      timer1 = millis();
      /* calculate meters/min*/
      Ms = calcmetersmin();
      //Serial.println(Ms);
      /*calculate moving average of strokes/min*/
      old_spm = (int)spm;
      calcstmra();
#if SENSOR_MODE == SENSOR_PULLEY
      spm = pulleySpm;
#else
      spm = stm_RA.getAverage()*60;
#endif

      // connecting
      if (deviceConnected && !oldDeviceConnected) {
        // do stuff here on connecting
        oldDeviceConnected = deviceConnected;
        //delay(5000);    
      }

      if (deviceConnected) {        //** Send a value to protopie. The value is in txValue **//

        // (the original used milliseconds*1000 in a 32-bit long and integer divisions: the averages
        //  were 0 / infinite and elapsed time wrapped after ~36 min)
        float elapsedS = movingMs / 1000.0f;   // moving time (timer pauses when standing still)
        if (elapsedS < 1.0f) elapsedS = 1.0f;
        float avrMs = meters / elapsedS;                       // average speed [m/s]
        rdKpi.strokeRate = (int)round(spm + old_spm);          // 0.5 strokes/min units
        rdKpi.strokeCount = strokes;
        rdKpi.averageStokeRate = constrain((int)round(strokes * 120.0f / elapsedS), 0, 255);
        rdKpi.totalDistance = meters;
        rdKpi.instantaneousPace = paceSecPer500(Ms);
        rdKpi.averagePace = paceSecPer500(avrMs);
        rdKpi.instantaneousPower = (int)round(2.8f * Ms * Ms * Ms); //https://www.concept2.com/indoor-rowers/training/calculators/watts-calculator
        rdKpi.averagePower = (int)round(2.8f * avrMs * avrMs * avrMs);
        rdKpi.elapsedTime = (int)constrain(elapsedS, 0.0f, 65535.0f);   // seconds

        setCxRowerData();
        //setCxLightRowerData();
        //delay(500); // bluetooth stack will go into congestion, if too many packets are sent
        SerialDebug.println("Send data " + String(rdKpi.strokeCount));

        delay(10);
        setCxBattery();

        //Why coxswain need this notify for start???
        char cRower[3];
        cRower[0]=0x01;
        cRower[1]=0x00;
        cRower[2]=0x00;
        pDtCharacteristic->setValue((uint8_t* )cRower, 3);
        pDtCharacteristic->notify();

      }
 
      // disconnecting
      if (!deviceConnected && oldDeviceConnected) {
        //delay(500); // give the bluetooth stack the chance to get things ready
        pServer->startAdvertising(); // restart advertising
        SerialDebug.println("start advertising");
        oldDeviceConnected = deviceConnected;
        resetTimer = 21;
      }

      if (resetTimer > 0) {
        if (resetTimer == 1) {
          resetTimer = 0;
          clicks = 0;
          break;
        }
        resetTimer = resetTimer - 1;
      }

      //battery 3.2 - 4.2V
      battery_RA.addValue(readBattery127());

    }
    
    updateMovingTime();
    pulseLogFlush();

    static unsigned long lastDrawMs = 0;
    if (millis() - lastDrawMs >= (rowerIdle() ? DISPLAY_REFRESH_IDLE_MS : DISPLAY_REFRESH_ROWING_MS)) {
      lastDrawMs = millis();
      drawRowingScreen();
    }

    // no pulses for a while -> deep sleep (wakes on next pulse)
    if (idleTooLong()) goToSleep();

    /* if select button is pressed reset */
    switch (read_LCD_buttons())
    {
      case btnSELECT:
        {
          reset();
          break;
        }
      case btnUP:
        {
          if (stage < 6){
            stage ++;
            delay(500);
            //lcd.clear();
          }
          break;
        }
      case btnDOWN:
      {
        if (stage > 0){
          stage --;
          delay(500);
          //lcd.clear();
        }
        break;
      }
    }

/*-------SIMULATE ROWING-------------*/
//      if (random(0, 100) < 30) {
//         rowerinterrupt();
//         }
/*-------SIMULATE ROWING-------------*/

  }

  //SerialDebug.println("*****EXIT*********");

}


void loop() {
  row_start();
}
