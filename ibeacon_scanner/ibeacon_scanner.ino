/*
こちらが最新である．2021/04/21
*/

#define GID_TO_DETECT 1000
// #define DISABLE_GID_CHECK

#define USE_M5STICKC
//#define USE_M5STACK
//#define USE_M5STICKCPLUS
//#define USE_ESP32MINIKIT

#define DEBUG
//#define NO_WEBSERVER
//#define NO_OTA

// if you want to use default prefs
//#define FORCE_DEFAULT

#define DEFAULT_HOSTNAME "sb_mowat_2F_1"
#define DEFAULT_ENDPOINT "http://192.168.3.45:3001"
#define DEFAULT_DETECTOR_ID 1
#define DEFAULT_PLACE "mowat_2F"

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <HTTPClient.h>
#ifdef USE_M5STICKC
#include <M5StickC.h>
#endif
#ifdef USE_M5STICKCPLUS
#include <M5StickCPlus.h>
#endif
#ifdef USE_M5STACK
#include <M5Stack.h>
#endif
#include <Preferences.h>
#ifndef NO_WEBSERVER
#include <WebServer.h>
#endif
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_wifi.h>

#ifdef USE_ESP32MINIKIT
#define LED_ON digitalWrite(GPIO_NUM_2, LOW)
#define LED_OFF digitalWrite(GPIO_NUM_2, HIGH)
#endif
#ifdef USE_M5STICKC
#define LED_ON digitalWrite(GPIO_NUM_10, LOW)
#define LED_OFF digitalWrite(GPIO_NUM_10, HIGH)
#endif
#ifdef USE_M5STICKCPLUS
#define LED_ON digitalWrite(GPIO_NUM_10, LOW)
#define LED_OFF digitalWrite(GPIO_NUM_10, HIGH)
#endif
#ifdef USE_M5STACK
#define LED_ON ;
#define LED_OFF ;
#endif

#ifndef NO_WEBSERVER
WebServer webServer(80);
#endif
Preferences prefs;
BLEScan *scan;
uint32_t ptime, screen_timer;
HTTPClient http;
uint8_t btn_pressed;
bool valid_db_server = true;
int found_ble_count = 0; 

String myhost, url_endpoint, api_beaconadd, api_beaconalive, place;
int detector_id = 0;
int alive_error_count = 0;

// CompanyId取得
unsigned short getCompanyId(BLEAdvertisedDevice device) {
  const unsigned short *pCompanyId =
      (const unsigned short *)&device.getManufacturerData().c_str()[0];
  // Serial.printf("companyID: %04x\n",*pCompanyId);
  return *pCompanyId;
}

// iBeacon Header取得
unsigned short getIBeaconHeader(BLEAdvertisedDevice device) {
  const unsigned short *pHeader =
      (const unsigned short *)&device.getManufacturerData().c_str()[2];
  // Serial.printf("Header: %04x\n",*pHeader);
  return *pHeader;
}

bool isIBeacon(BLEAdvertisedDevice device) {
  if (getCompanyId(device) != 0x004C) {
    return false;
  }
  if (getIBeaconHeader(device) != 0x1502) {
    return false;
  }
  return true;
}

// UUID取得
String getUuid(BLEAdvertisedDevice device) {
  const char *pUuid = &device.getManufacturerData().c_str()[4];
  char uuid[64] = {0};
  sprintf(
      uuid,
      "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
      pUuid[0], pUuid[1], pUuid[2], pUuid[3], pUuid[4], pUuid[5], pUuid[6],
      pUuid[7], pUuid[8], pUuid[9], pUuid[10], pUuid[11], pUuid[12], pUuid[13],
      pUuid[14], pUuid[15]);
  return String(uuid);
}

// major 取得
unsigned short getMajor(BLEAdvertisedDevice device) {
  const unsigned short *ph =
      (const unsigned short *)&device.getManufacturerData().c_str()[20];
  const unsigned char *pl =
      (const unsigned char *)&device.getManufacturerData().c_str()[21];
  // Serial.printf("Major: %d\n", (*ph & 0x00ff) << 8 | *pl);
  return (*ph & 0x00ff) << 8 | *pl;
}

// minor 取得
unsigned short getMinor(BLEAdvertisedDevice device) {
  const unsigned short *ph =
      (const unsigned short *)&device.getManufacturerData().c_str()[22];
  const unsigned char *pl =
      (const unsigned char *)&device.getManufacturerData().c_str()[23];
  // Serial.printf("Minor: %d\n", (*ph & 0x00ff) << 8 | *pl);
  return (*ph & 0x00ff) << 8 | *pl;
}

// TxPower取得
signed char getTxPower(BLEAdvertisedDevice device) {
  const signed char *pTxPower =
      (const signed char *)&device.getManufacturerData().c_str()[24];
  return *pTxPower;
}

// iBeaconの情報をシリアル出力
void printIBeacon(BLEAdvertisedDevice device) {
  Serial.printf("[iBeacon] rssi:%d minor:%d power:%d\r\n",
                device.getRSSI(),
                getMinor(device), getTxPower(device));
}

void postAlive() {
  char params[128];

  String apiurl = url_endpoint + api_beaconalive;
  String ipaddress = WiFi.localIP().toString();

  http.begin(apiurl.c_str());
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  sprintf(params, "ipaddress=%s&room=%s&id=%d", ipaddress.c_str(),
          place.c_str(), detector_id);
  int httpCode = http.POST(params);
  if (httpCode > 0) {
    LED_OFF;
#ifdef DEBUG
    Serial.printf("[HTTP] POST alive... code: %d\n", httpCode);
    Serial.printf("[HTTP] url: %s params: %s\n", apiurl.c_str(), params);
#endif
    valid_db_server = true;
    alive_error_count = 0;
  } else {
    LED_ON;
    Serial.printf("[ERROR] POST alive...: %s\n",
                  http.errorToString(httpCode).c_str());
    Serial.printf("[ERROR] url: %s params: %s\n", apiurl.c_str(), params);
    valid_db_server = false;
    alive_error_count++;
    if (alive_error_count > 2) {
      ESP.restart();
      while (1)
        ;
    }
  }
  http.end();
}

void postIBeacon(BLEAdvertisedDevice device) {
  char params[128];
  unsigned short label;
  float proxi;

  if (!valid_db_server) {
#ifdef USE_M5STICKC
    M5.Lcd.fillCircle(150, 20, 5, TFT_BLACK);
    M5.Lcd.drawCircle(150, 20, 5, TFT_GREEN);
#endif
#ifdef USE_M5STICKCPLUS
    M5.Lcd.fillCircle(230, 20, 5, TFT_BLACK);
    M5.Lcd.drawCircle(230, 20, 5, TFT_GREEN);
#endif
#ifdef USE_M5STACK
    M5.Lcd.fillCircle(230, 100, 5, TFT_BLACK);
    M5.Lcd.drawCircle(230, 100, 5, TFT_GREEN);
#endif
    return;
  }

  String apiurl = url_endpoint + api_beaconadd;
  label = getMinor(device);
  proxi = pow(10, ((getTxPower(device) - device.getRSSI()) / 20.0));
  http.begin(apiurl.c_str());
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  sprintf(params, "label=%d&place=%s&proxi=%f&id=%d", label, place.c_str(),
          proxi, detector_id);
  int httpCode = http.POST(params);
  if (httpCode > 0) {
    LED_OFF;
#ifdef USE_M5STICKC
    M5.Lcd.fillCircle(150, 20, 5, TFT_GREEN);
#endif
#ifdef USE_M5STICKCPLUS
    M5.Lcd.fillCircle(230, 20, 5, TFT_GREEN);
#endif
#ifdef USE_M5STACK
    M5.Lcd.fillCircle(230, 100, 5, TFT_GREEN);
#endif
#ifdef DEBUG
    Serial.printf("[HTTP] POST ibeacon... code: %d\n", httpCode);
#endif
  } else {
    LED_ON;
#ifdef USE_M5STICKC
    M5.Lcd.fillCircle(150, 20, 5, TFT_BLACK);
    M5.Lcd.drawCircle(150, 20, 5, TFT_GREEN);
#endif
#ifdef USE_M5STICKCPLUS
    M5.Lcd.fillCircle(230, 20, 5, TFT_BLACK);
    M5.Lcd.drawCircle(230, 20, 5, TFT_GREEN);
#endif
#ifdef USE_M5STACK
    M5.Lcd.fillCircle(230, 100, 5, TFT_BLACK);
    M5.Lcd.drawCircle(230, 100, 5, TFT_GREEN);
#endif
    Serial.printf(
        "[ERROR] POST ibeacon... : %s\n",
        http.errorToString(httpCode).c_str());
    Serial.printf("[ERROR] url: %s params: %s\n", apiurl.c_str(), params);
  }
  http.end();
}

#ifndef NO_WEBSERVER
void handleStatus() {
  String message;
  DynamicJsonBuffer jsonBuffer;
  JsonObject &json = jsonBuffer.createObject();
  json["place"] = place;
  json["detector_id"] = detector_id;
  //  json["timestamp"] = timestamp();
  json.printTo(message);
  webServer.send(200, "application/json", message);
}

void handleConfig() {
  String argname, argv, message;
  for (int i = 0; i < webServer.args(); i++) {
    argname = webServer.argName(i);
    argv = webServer.arg(i);
    if (argname == "hostname") {
      myhost = argv;
      prefs.putString("hostname", myhost);
    }
    if (argname == "url_endpoint") {
      url_endpoint = argv;
      prefs.putString("url_endpoint", url_endpoint);
      delay(100);
      //      postAlive();
    }
    if (argname == "api_beaconadd") {
      api_beaconadd = argv;
      prefs.putString("api_beaconadd", api_beaconadd);
    }
    if (argname == "api_beaconalive") {
      api_beaconalive = argv;
      prefs.putString("api_beaconalive", api_beaconalive);
    }
    if (argname == "place") {
      place = argv;
      prefs.putString("place", place);
    }
    if (argname == "detector_id") {
      detector_id = argv.toInt();
      prefs.putInt("detector_id", detector_id);
    }
    //     else if (argname == "maxactivetime") {
    //      max_active_time = argv.toInt();
    //      prefs.putUInt("max_active_time", max_active_time);
    //    }
  }
  DynamicJsonBuffer jsonBuffer;
  JsonObject &json = jsonBuffer.createObject();
  json["hostname"] = myhost;
  json["url_endpoint"] = url_endpoint;
  json["api_beaconadd"] = api_beaconadd;
  json["api_beaconalive"] = api_beaconalive;
  //  json["api_logadd"] = api_logadd;
  json["place"] = place;
  json["detector_id"] = detector_id;
  //  json["timestamp"] = timestamp();
  json.printTo(message);
  webServer.send(200, "application/json", message);
}

#endif

void btnHandler() { btn_pressed = 1; }

void setup() {
  float accX, accY, accZ;
  WiFiManager wifiManager;

  btn_pressed = 0;
  screen_timer = 0;

  Serial.begin(115200);
  prefs.begin("ibeacon", false);

#ifdef FORCE_DEFAULT
//prefs.clear();
  prefs.putString("hostname", DEFAULT_HOSTNAME);
  prefs.putString("url_endpoint", DEFAULT_ENDPOINT);
  prefs.putString("api_beaconadd", "/beacon/add");
  prefs.putString("api_beaconalive", "/beacon/alive");
  prefs.putString("place", DEFAULT_PLACE);
  prefs.putInt("detector_id", DEFAULT_DETECTOR_ID);
#endif
  
  myhost = prefs.getString("hostname", DEFAULT_HOSTNAME);
  url_endpoint = prefs.getString("url_endpoint", DEFAULT_ENDPOINT);
  api_beaconadd = prefs.getString("api_beaconadd", "/beacon/add");
  api_beaconalive = prefs.getString("api_beaconalive", "/beacon/alive");
  detector_id = prefs.getInt("detector_id", DEFAULT_DETECTOR_ID);
  place = prefs.getString("place", DEFAULT_PLACE);

  int orient = 1;
#ifdef USE_M5STICKC
  pinMode(GPIO_NUM_37, INPUT_PULLUP);
  attachInterrupt(GPIO_NUM_37, btnHandler, FALLING);
  pinMode(GPIO_NUM_10, OUTPUT);
  LED_OFF;
  M5.begin(true,true,true);
  M5.IMU.Init();
  M5.IMU.getAccelData(&accX, &accY, &accZ);
  orient = accX < 0 ? 3 : 1;
  Serial.printf("accx:%f accy:%f accz:%f", accX, accY, accZ);
  M5.Lcd.fillScreen(BLACK);
  M5.Axp.ScreenBreath(12);
  M5.Lcd.setRotation(orient); // 本体の向きに応じて
  M5.Lcd.setTextFont(2);
  M5.Lcd.setTextDatum(0);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(0, 20);
  M5.Lcd.print("Connect WiFi AP");
  M5.Lcd.setCursor(0, 40);
  M5.Lcd.setTextColor(RED);
  M5.Lcd.print("  BEACON_SCAN  ");
#endif
#ifdef USE_M5STICKCPLUS
  pinMode(GPIO_NUM_37, INPUT_PULLUP);
  attachInterrupt(GPIO_NUM_37, btnHandler, FALLING);
  pinMode(GPIO_NUM_10, OUTPUT);
  LED_OFF;
  M5.begin(true,true,true);
  M5.IMU.Init();
  M5.IMU.getAccelData(&accX, &accY, &accZ);
  orient = accX < 0 ? 3 : 1;
  Serial.printf("accx:%f accy:%f accz:%f", accX, accY, accZ);
  M5.Lcd.fillScreen(BLACK);
  M5.Axp.ScreenBreath(12);
  M5.Lcd.setRotation(orient); // 本体の向きに応じて
  M5.Lcd.setTextFont(2);
  M5.Lcd.setTextDatum(0);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(40, 45);
  M5.Lcd.print("Connect WiFi AP");
  M5.Lcd.setCursor(40, 60);
  M5.Lcd.setTextColor(RED);
  M5.Lcd.print("  BEACON_SCAN  ");
#endif
#ifdef USE_M5STACK
  pinMode(GPIO_NUM_38, INPUT_PULLUP);
  attachInterrupt(GPIO_NUM_38, btnHandler, FALLING);
  M5.begin();
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setBrightness(200);
  M5.Lcd.setRotation(orient); // 本体の向きに応じて
  M5.Lcd.setTextFont(2);
  M5.Lcd.setTextDatum(0);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(80, 100);
  M5.Lcd.print("Connect WiFi AP");
  M5.Lcd.setCursor(80, 120);
  M5.Lcd.setTextColor(RED);
  M5.Lcd.print("  BEACON_SCAN  ");
#endif

  wifiManager.setConfigPortalTimeout(180);
  wifiManager.autoConnect("BEACON_SCAN");

#ifdef USE_M5STICKC
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextFont(8);
  M5.Lcd.setTextColor(BLUE);
  M5.Lcd.setTextDatum(0);
  M5.Lcd.setCursor(80, 0);
  M5.Lcd.printf("%d", detector_id);
  M5.Lcd.setTextFont(4);
  M5.Lcd.setTextDatum(0);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(0, 26);
  M5.Lcd.printf("%s", place);
  M5.Lcd.setTextFont(0);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.print(WiFi.localIP());
  M5.Axp.SetLDO2(true);
#endif
#ifdef USE_M5STICKCPLUS
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextFont(8);
  M5.Lcd.setTextColor(BLUE);
  M5.Lcd.setTextDatum(0);
  M5.Lcd.setCursor(120, 25);
  M5.Lcd.printf("%d", detector_id);
  M5.Lcd.setTextFont(4);
  M5.Lcd.setTextDatum(0);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(40, 51);
  M5.Lcd.printf("%s", place);
  M5.Lcd.setTextFont(0);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.print(WiFi.localIP());
  M5.Axp.SetLDO2(true);
#endif
#ifdef USE_M5STACK
  M5.Lcd.setTextSize(1);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextFont(8);
  M5.Lcd.setTextColor(BLUE);
  M5.Lcd.setTextDatum(0);
  M5.Lcd.setCursor(160, 80);
  M5.Lcd.printf("%d", detector_id);
  M5.Lcd.setTextFont(4);
  M5.Lcd.setTextDatum(0);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(80, 106);
  M5.Lcd.printf("%s", place);
  M5.Lcd.setTextFont(0);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.print(WiFi.localIP());
//  M5.Lcd.setBrightness(0);
//  M5.Lcd.sleep();
#endif

  ArduinoOTA.setHostname(myhost.c_str());
  ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";

        Serial.println("Start updating " + type);
        // pinMode(GPIO_NUM_10, OUTPUT);
      })
      .onEnd([]() {
        LED_OFF;
        Serial.println("\nEnd");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
#ifdef USE_M5STICKC
        digitalWrite(GPIO_NUM_10, (progress / (total / 100)) % 2);
#endif
#ifdef USE_M5STICKCPLUS
        digitalWrite(GPIO_NUM_10, (progress / (total / 100)) % 2);
#endif
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
          Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
          Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
          Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
          Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR)
          Serial.println("End Failed");
      });
#ifndef NO_OTA
  ArduinoOTA.begin();
#endif

#ifndef NO_WEBSERVER
  webServer.on("/", handleStatus);
  webServer.on("/status", handleStatus);
  webServer.on("/config", handleConfig);
  webServer.begin();
  Serial.println("web server start.");
#endif

  //  postAlive();

  BLEDevice::init("");
  scan = BLEDevice::getScan();
//  scan->setAdvertisedDeviceCallbacks(new IBeaconAdvertised(), true);
  scan->setActiveScan(false);
//  scan->setInterval(100);
//  scan->setWindow(99);
  Serial.println("ble scanner start.");
  ptime = millis();
  screen_timer = millis();
//  scan->start(0);
}

void loop() {
  float accX, accY, accZ;
  int orient = 1;
#ifndef NO_WEBSERVER
  webServer.handleClient();
#endif
#ifndef NO_OTA
  ArduinoOTA.handle();
#endif

#ifdef USE_M5STACK
  M5.update();
#endif

  if (millis() > ptime + (1000 * 60)) {
    if (found_ble_count == 0) {
      ESP.restart();
      while(1){};
    }
    postAlive();
    ptime = millis();
    found_ble_count = 0;
  }
  if (btn_pressed) {
#ifdef USE_M5STICKC
    M5.Lcd.fillScreen(BLACK);
    M5.IMU.getAccelData(&accX, &accY, &accZ);
    Serial.printf("accx:%f accy:%f accz:%f\n", accX, accY, accZ);
    orient = accX < 0 ? 3 : 1;
    M5.Axp.ScreenBreath(12);
    M5.Lcd.setRotation(orient); // 本体の向きに応じて
    M5.Lcd.setTextSize(1);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextFont(8);
    M5.Lcd.setTextColor(BLUE);
    M5.Lcd.setTextDatum(0);
    M5.Lcd.setCursor(80, 0);
    M5.Lcd.printf("%d", detector_id);
    M5.Lcd.setTextFont(4);
    M5.Lcd.setTextDatum(0);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(0, 26);
    M5.Lcd.printf("%s", place);
    M5.Lcd.setTextFont(0);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.print(WiFi.localIP());
    M5.Axp.SetLDO2(true);
#endif
#ifdef USE_M5STICKCPLUS
    M5.Lcd.fillScreen(BLACK);
    M5.IMU.getAccelData(&accX, &accY, &accZ);
    Serial.printf("accx:%f accy:%f accz:%f\n", accX, accY, accZ);
    orient = accX < 0 ? 3 : 1;
    M5.Axp.ScreenBreath(12);
    M5.Lcd.setRotation(orient); // 本体の向きに応じて
    M5.Lcd.setTextSize(1);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextFont(8);
    M5.Lcd.setTextColor(BLUE);
    M5.Lcd.setTextDatum(0);
    M5.Lcd.setCursor(120, 25);
    M5.Lcd.printf("%d", detector_id);
    M5.Lcd.setTextFont(4);
    M5.Lcd.setTextDatum(0);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(40, 51);
    M5.Lcd.printf("%s", place);
    M5.Lcd.setTextFont(0);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.print(WiFi.localIP());
    M5.Axp.SetLDO2(true);
#endif
#ifdef USE_M5STACK
    M5.Lcd.wakeup();
    M5.Lcd.setBrightness(200);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setRotation(orient);
    M5.Lcd.setTextSize(1);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextFont(8);
    M5.Lcd.setTextColor(BLUE);
    M5.Lcd.setTextDatum(0);
    M5.Lcd.setCursor(160, 80);
    M5.Lcd.printf("%d", detector_id);
    M5.Lcd.setTextFont(4);
    M5.Lcd.setTextDatum(0);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setCursor(80, 106);
    M5.Lcd.printf("%s", place);
    M5.Lcd.setTextFont(0);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.print(WiFi.localIP());
#endif
    btn_pressed = 0;
    screen_timer = millis();
  }
  if (screen_timer > 0) {
    if (millis() > screen_timer + 20000) {
#ifdef USE_M5STICKC
      M5.Axp.SetLDO2(false);
#endif
#ifdef USE_M5STICKCPLUS
      M5.Axp.SetLDO2(false);
#endif
#ifdef USE_M5STACK
      M5.Lcd.setBrightness(0);
      M5.Lcd.sleep();
#endif
      screen_timer = 0;
    }
  }

#ifdef USE_M5STICKC
  M5.Lcd.fillCircle(150, 5, 5, TFT_BLUE);
#endif
#ifdef USE_M5STICKCPLUS
  M5.Lcd.fillCircle(230, 5, 5, TFT_BLUE);
#endif
#ifdef USE_M5STACK
  M5.Lcd.fillCircle(230, 85, 5, TFT_BLUE);
#endif
  //  scan->start(1, false);
  BLEScanResults foundDevices = scan->start(2);
  int count = foundDevices.getCount();
  found_ble_count += count;
#ifdef DEBUG
    Serial.printf("[BLE] total detected: %d\n", found_ble_count);
#endif
  for (int i = 0; i < count; i++) {
    BLEAdvertisedDevice d = foundDevices.getDevice(i);
#ifdef DEBUG
    Serial.printf("[BLE] detected: %s\n", d.toString().c_str());
#endif
    if (d.haveManufacturerData()) {
      if (isIBeacon(d)) {
        if (getMajor(d) == GID_TO_DETECT) {
          printIBeacon(d);
          postIBeacon(d);
        }
      }
    }
  }
#ifdef USE_M5STICKC
  M5.Lcd.fillCircle(150, 5, 5, TFT_BLACK);
#endif
#ifdef USE_M5STICKCPLUS
  M5.Lcd.fillCircle(230, 5, 5, TFT_BLACK);
#endif
#ifdef USE_M5STACK
  M5.Lcd.fillCircle(230, 85, 5, TFT_BLACK);
#endif
  delay(1);
}