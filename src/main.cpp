////
//// humidity controller
////

#include <Arduino.h>


///
/// ENV IV Unit
///

// SHT4X tmperature and humidity device
#include <SensirionI2cSht4x.h>
#include <Wire.h> // should be included before M5Unified.h

// https://github.com/Sensirion/arduino-i2c-sht4x
// macro definitions
// make sure that we use the proper definition of NO_ERROR
#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

SensirionI2cSht4x sensor;
static char errorMessage[64];
static int16_t error;
static float aTemperature = 0.0;
static float aHumidity = 0.0;


bool initSHT4X() {

  Serial.println("init SHT4X");
  Wire.begin();
  sensor.begin(Wire, SHT40_I2C_ADDR_44);

  sensor.softReset();
  delay(10);
  uint32_t serialNumber = 0;
  error = sensor.serialNumber(serialNumber);
  if (error != NO_ERROR) {
    Serial.print("Error trying to execute serialNumber(): ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
    return false;
  }
  Serial.print("serialNumber: ");
  Serial.print(serialNumber);
  Serial.println();
  return true;

}



///
/// M5unified
///

// Include this to enable the M5 global instance.
#include <M5Unified.h>

static int h; // it holds M5.Display.fontHeight()

bool initDisplay() {

  auto cfg = M5.config(); // get default configuration
  M5.begin(cfg); // should be located before initSHT4X
  M5.Power.begin();
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setFont(&fonts::Font0); // default
  h = M5.Display.fontHeight();
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);

  return true;
}


void message(const char *message, int x, int y) {
  Serial.println(message);
  M5.Lcd.setCursor(x, h*y);
  M5.Lcd.println(message);
}



///
/// BLE device
///

// https://h2zero.github.io/NimBLE-Arduino/md__new_user_guide.html#autotoc_md45
// Creating a Client
#include <NimBLEDevice.h>

// SwitchBot UUIDs
const NimBLEUUID serviceUUID("cba20d00-224d-11e6-9fb8-0002a5d5c51b");
const NimBLEUUID charUUID("cba20002-224d-11e6-9fb8-0002a5d5c51b");

// each device has its own service and characteristic
const NimBLEAddress botAddress("e4:a7:48:ce:70:92");
static NimBLEAdvertisedDevice *botDevice;
static NimBLEClient *botClient;
static NimBLERemoteService *botService;
static NimBLERemoteCharacteristic *botCharacteristic;

const NimBLEAddress plugMiniAddress("c0:4e:30:94:bf:4e");
static NimBLEAdvertisedDevice *plugMiniDevice;
static NimBLEClient *plugMiniClient;
static NimBLERemoteService *plugMiniService;
static NimBLERemoteCharacteristic *plugMiniCharacteristic;

// PlugMini commands
const uint8_t cmdOn[] = {0x57, 0x0f, 0x50, 0x01, 0x01, 0x80};
const uint8_t cmdOff[] = {0x57, 0x0f, 0x50, 0x01, 0x01, 0x00};
const uint8_t cmdToggle[] = {0x57, 0x0f, 0x50, 0x01, 0x02, 0x80};

// Bot command
const uint8_t cmdPress[] = {0x57, 0x01, 0x00};


bool initBLEdevices() {

  NimBLEDevice::init("");

  NimBLEScan *pScan = NimBLEDevice::getScan();
  pScan->setInterval(45);
  pScan->setWindow(15);
  pScan->setActiveScan(true);
  // Serial.println("BLE scan for 5 seconds");
  // M5.Lcd.setCursor(0, h*15);
  // M5.Lcd.println("BLE scan for 5 seconds");
  message("BLE scan for 5 seconds", 0, 15);
  NimBLEScanResults results = pScan->start(10);
  message("done                  ", 0, 15);

  plugMiniDevice = results.getDevice(plugMiniAddress);
  if (plugMiniDevice == nullptr) {
    return false;
  }

  botDevice = results.getDevice(botAddress);
  if (botDevice == nullptr) {
    return false;
  }

  // all davices are recognized, so stop scan to communicate with them
  pScan->stop();

  // setup client, service, and characteristic for each device

  plugMiniClient = NimBLEDevice::createClient();
  if (plugMiniClient->connect(plugMiniDevice)) {
    plugMiniService = plugMiniClient->getService(serviceUUID);
    if (plugMiniService != nullptr) {
      plugMiniCharacteristic = plugMiniService->getCharacteristic(charUUID);
      if (plugMiniCharacteristic != nullptr) {
        if (plugMiniClient->disconnect() != 0) {
          return false;
        }
      } else {
        return false;
      }
    } else {
      return false;  
    }
  } else {
    return false;
  }

  botClient = NimBLEDevice::createClient();
  if (botClient->connect(botDevice)) {
    botService = botClient->getService(serviceUUID);
    if (botService != nullptr) {
      botCharacteristic = botService->getCharacteristic(charUUID);
      if (botCharacteristic != nullptr) {
        if (botClient->disconnect() != 0) {
          return false;
        }
      } else {
        return false;
      }
    } else {
      return false;  
    }
  } else {
    return false;
  }

  return true;
}


// void sendBLTCommand(NimBLEClient *pClient, 
//   NimBLERemoteCharacteristic *characteristic, 
//   const uint8_t *command) {
//   pClient->connect();
//   characteristic->writeValue(command, sizeof(command));
//   pClient->disconnect();
// }



///
/// main logic
///

// master and previous state
enum {
  S_0 = 0,
  S_1,
  S_2,
};
static int mState;
static int prevState;


void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(100);
  }

  if (!initDisplay()) {
    Serial.println("init display filure, halt");
    while (1) ; // in case M5.Power.powerOff() cannot be available
    // NOTREACHED
  }

  if (!initSHT4X()) { // false if error
    message("init SHT4X failure. shutdown", 0, 15);
    M5.Power.powerOff();
    // NOTREACHED
  }

  if (!initBLEdevices()) { // false if error
    message("init BLE device failure. shutdown", 0, 15);
    M5.Power.powerOff();
    // NOTREACHED
  }

  error = sensor.measureHighPrecision(aTemperature, aHumidity);
  if (error != NO_ERROR) {
    Serial.print("Error trying to execute measureHighPrecision(): ");
    errorToString(error, errorMessage, sizeof errorMessage);
    message(errorMessage, 0, 15);
    message("shutdown", 0, 16);
    M5.Power.powerOff() ;
    // NOTREACHED
  }

  // setup initial state
  if (aHumidity <= 47.0) {
    prevState = S_1; // too dry, needs wet
  } else if (aHumidity >= 53.0) {
    prevState = S_2; // too wet, needs dry
  } else { // > 47.0 && < 53.0
    prevState = S_0; // OK
  }
}


void loop()
{
  int h = M5.Display.fontHeight();

  error = sensor.measureHighPrecision(aTemperature, aHumidity);
  if (error != NO_ERROR) {
    Serial.print("Error trying to execute measureLowestPrecision(): ");
    errorToString(error, errorMessage, sizeof errorMessage);
    message(errorMessage, 0, 15);
    message("shutdown", 0, 16);
    M5.Power.powerOff() ;
  }
  Serial.print("aTemperature: ");
  Serial.print(aTemperature);
  Serial.print("\t");
  Serial.print("aHumidity: ");
  Serial.print(aHumidity);
  Serial.println();

  M5.Lcd.setCursor(0, h*3);
  M5.Lcd.printf("Temp: %.2f ", aTemperature);
  M5.Lcd.printf("Humid: %.2f", aHumidity);

  // state machine
  switch (prevState) {
  case S_0: // wet off and dry off for needed
    if (aHumidity < 46.0) { // 47.0 - 1.0
      mState = S_1;
      message("wet on ", 0, 15);
      botClient->connect();
      botCharacteristic->writeValue(cmdPress, sizeof(cmdPress));
      botClient->disconnect();
      break;
    } else if (aHumidity > 54.0) { // 53.0 + 1.0
      mState = S_2;
      message("dry on ", 0, 15);
      plugMiniClient->connect();
      plugMiniCharacteristic->writeValue(cmdOn, sizeof(cmdOn));
      plugMiniClient->disconnect();
      break;
    } else {
      mState = prevState;
      break;
    }
  case S_1: // wet on and dry off if needed
    if (aHumidity > 54.0) { // 53.0 + 1.0
      mState = S_2;
      message("wet off", 0, 15);
      botClient->connect();
      botCharacteristic->writeValue(cmdPress, sizeof(cmdPress));
      botClient->disconnect();
      message("dry on ", 0, 15);
      plugMiniClient->connect();
      plugMiniCharacteristic->writeValue(cmdOn, sizeof(cmdOn));
      plugMiniClient->disconnect();
      break;
    } else if (aHumidity > 48.0) { // 47.0 + 1.0
      mState = S_0;
      message("wet off", 0, 15);
      botClient->connect();
      botCharacteristic->writeValue(cmdPress, sizeof(cmdPress));
      botClient->disconnect();
      break;
    } else {
      mState = prevState;
      break;
    }
  case S_2: // wet off and dry on if needed
    if (aHumidity < 46.0) { // 47.0 - 1.0
      mState = S_1;
      message("dry off", 0, 15);
      plugMiniClient->connect();
      plugMiniCharacteristic->writeValue(cmdOff, sizeof(cmdOff));
      plugMiniClient->disconnect();
      message("wet on ", 0, 15);
      botClient->connect();
      botCharacteristic->writeValue(cmdPress, sizeof(cmdPress));
      botClient->disconnect();
      break;
    } else if (aHumidity < 52.0) { // 53.0 - 1.0
      mState = S_0;
      message("dry off", 0, 15);
      plugMiniClient->connect();
      plugMiniCharacteristic->writeValue(cmdOff, sizeof(cmdOff));
      plugMiniClient->disconnect();
      break;
    } else {
      mState = prevState;
      break;
    }
  default:
    break;
  }
  prevState = mState;

  delay(5000);
}
