#include <WiFi.h>
#include <DNSServer.h>
#include "ESPmDNS.h"
#include <SocketIOclient.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <time.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "ad7177_lib.h"
#include "utility.h"
#include <string>
#include <deque>


#define USE_LIB_WEBSOCKET true
#define WEBSOCKET_DISABLED true
#include "RemoteDebug.h"

time_t now;
tm timeinfo;

/****************************************
 * Wifi manager
 ***************************************/
WiFiManager wm;

/****************************************
 * OTA Service
 ***************************************/
const char *OTAName     = SECRET_OTA_NAME;
const char *OTAPassword = SECRET_OTA_PASS;

/****************************************
 * AsyncWebServer
 ***************************************/
AsyncWebServer server(80);

/****************************************
 * WebSocketsServer
 ***************************************/
WebSocketsServer websocket(81);
bool is_websocket_connected = false;

ws_conn_cb_t ws_disconnected_cb = NULL;
ws_conn_cb_t ws_connected_cb = NULL;
ws_text_cb_t ws_text_cb = NULL;

/****************************************
 *  Remote debug
 ***************************************/
// Disable remote debug for production build
//#define DEBUG_DISABLED true

#ifndef DEBUG_DISABLED
  RemoteDebug Debug;
#endif

/****************************************
 *  Logging
 ***************************************/

#define MAX_LOG_ENTRIES 50

// Queue to store log messages using std::string
std::deque<std::string> log_buffer;



/**********************************************************
 *
 * MDNS Functions
 *
 *********************************************************/

// Setup multicast DNS name
void dns_init() {
  String hostNameWifi = HOST_NAME;
  hostNameWifi.concat(".local");

  #if defined(ESP8266)
    WiFi.hostname(hostNameWifi);
  #elif  defined(ESP32)
    WiFi.setHostname(hostNameWifi.c_str());
  #endif

  if (MDNS.begin(HOST_NAME)) {
    Serial.print("* MDNS responder started. Hostname -> ");
    Serial.println(HOST_NAME);
  }
}

/**********************************************************
 *
 * Wifi Manager Functions
 *
 *********************************************************/
void wm_init() {
  wm.autoConnect("SetupSMU5522");
}

/**********************************************************
 *
 * OTA Functions
 *
 *********************************************************/
void ota_init() {
  ArduinoOTA.setHostname(OTAName);
  ArduinoOTA.setPassword(OTAPassword);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\r\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("OTA ready\r\n");
}

void ota_process() {
  ArduinoOTA.handle();  // OTA update
}

/**********************************************************
 *
 * Little FS Functions
 *
 *********************************************************/

void littlefs_init() {
  // Initialize LittleFS
  if(!LittleFS.begin()){
    Serial.println("An error has occurred while mounting LittleFS");
    return;
  }
  Serial.println("LittleFS mounted successfully");
}

void littlefs_listdir(fs::FS &fs, const char * dirname, uint8_t levels){
    debugD("Listing directory: %s", dirname);

    File root = fs.open(dirname);
    if(!root){
        debugD("- failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        debugD(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            debugD("  DIR : %s", file.name());
            if(levels){
                littlefs_listdir(fs, file.path(), levels -1);
            }
        } else {
            debugD("  FILE: %s\tSIZE: %d",file.name(),file.size());
        }
        file = root.openNextFile();
    }
}


/**********************************************************
 *
 * Websever Functions
 *
 *********************************************************/

void webserver_notfound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void webserver_init() {
  //server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
  //  request->send(200, "text/plain", "Hello, world");
  //});
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/websocket.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/websocket.js", "application/javascript");
  });

  server.on("/styles.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/styles.css", "text/css");
  });

  server.onNotFound(webserver_notfound);

  // Start server
  server.begin();
}

/**********************************************************
 *
 * Logging Functions
 *
 *********************************************************/

// Function to log messages
void log_add(const std::string& message) {
  std::string json_msg = "{\"log\": \"" + message + "\"}";

  // If a client is connected, send the message immediately
  if (is_websocket_connected) {
    websocket.broadcastTXT(json_msg.c_str());
  } else {
    // Store message in the buffer
    log_buffer.push_back(json_msg);

    // If the buffer exceeds MAX_LOG_ENTRIES, remove the oldest message
    if (log_buffer.size() > MAX_LOG_ENTRIES) {
      log_buffer.pop_front();
    }
  }
}

// Function to flush the buffer when a client connects
void log_flush() {
  for (const auto& message : log_buffer) {
    websocket.broadcastTXT(message.c_str());
  }
  log_buffer.clear();
}

/**********************************************************
 *
 * Websocket Functions
 *
 *********************************************************/

void websocket_set_cb(ws_conn_cb_t connected_cb, ws_conn_cb_t disconnected_cb, ws_text_cb_t text_cb) {
  ws_disconnected_cb = disconnected_cb;
  ws_connected_cb    = connected_cb;
  ws_text_cb         = text_cb;
}

// WebSocket event handler
void websocket_event(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

  switch (type) {
    case WStype_DISCONNECTED:             // if the websocket is disconnected
      is_websocket_connected = false;
      debugD("[%u] Disconnected!\n", num);
      if (ws_disconnected_cb) ws_disconnected_cb();
      break;
    case WStype_CONNECTED:                // if a new websocket connection is established
      is_websocket_connected = true;
      {  // set scope for ip
        IPAddress ip = websocket.remoteIP(num);
        debugD("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      }
      log_add("Client connected.");
      log_flush();
      if (ws_connected_cb) ws_connected_cb();
      break;
    case WStype_TEXT:                    // if new text data is received
      debugD("[%u] get Text: %s\n", num, payload);
      {  // set scope for json_str
        String json_str = String((char*) payload);
        DynamicJsonDocument json(1024);
        deserializeJson(json, json_str);
      }
      if (ws_text_cb) ws_text_cb(payload, length);
      break;
    default:
      break;
  }
}

// WebSocket init
void websocket_init() {
  // Setup WebSocket server
  websocket.begin();
  websocket.onEvent(websocket_event);
}

void websocket_process() {
  websocket.loop();     // Check for websocket events
}

void websocket_send(const char *message) {
  websocket.broadcastTXT(message);
}



/**********************************************************
 *
 * RemoteDebug Functions
 *
 *********************************************************/
void debug_init() {
  Debug.begin(HOST_NAME);         // Initialize the WiFi server
  Debug.setResetCmdEnabled(true); // Enable the reset command
  Debug.showProfiler(true);       // Enable time profiling
  Debug.showColors(true);         // Enable olors
  MDNS.addService("telnet", "tcp", 23);
}

void debug_process() {
  Debug.handle();       // remote debug
  String last_cmd = Debug.getLastCommand();
  Debug.clearLastCommand();
}

/**********************************************************
 *
 * ADC Functions
 *
 *********************************************************/
uint32_t adc_data;
uint32_t adc_stat;
uint32_t adc_count = 0;
float adc_ref = 5;

void adc_process(int64_t data){
  uint64_t udata = (uint64_t) data;
  char buf_hex[17]; // 16 characters for 64bit + null char
  sprintf(buf_hex, "%016X", data);

  String message = "{\"type\":\"data\", \"hex\":\"0x" + String(buf_hex) + "\", \"int64\":" + String(data) + ", \"uint64\":" + String(udata) + "}";
  websocket.broadcastTXT(message);
}

//void adc_process(uint32_t data){
//  debugV("In callback.");
//
//  adc_data = (data >> 8) & 0xFFFFFF;
//  adc_stat = (data & 0xFF);
//  adc_count++;
//
//  // Format ADC data and send to browsers
//  char buf_hex[9]; // 8 characters for 32bit + null char
//  char buf_stat[9]; // 8 characters for 32bit + null char
//  sprintf(buf_hex, "%06X", data);
//  sprintf(buf_stat, "%02X", adc_stat);
//  float adcVoltage = 2*adc_ref * (((float) adc_data)/((1<<24) -1 ) - 0.5);
//  String message = "{\"type\":\"adc_data\", \"hex\":\"0x" + String(buf_hex) + "\", \"value\":" + String(adc_data) + ",\"voltage\":" + String(adcVoltage) + ",\"num\":" + String(adc_count) + ",\"status\":\"0x" + String(buf_stat) + "\"}";
//  websocket.broadcastTXT(message);
//}
