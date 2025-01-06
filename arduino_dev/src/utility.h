#ifndef UTILITY_H
#define UTILITY_H

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
#include "quad_smu.h"
#include <string>

#define USE_LIB_WEBSOCKET true
#define WEBSOCKET_DISABLED true
#include "RemoteDebug.h"




/****************************************
 * Time and Time Zone
 ***************************************/
#define NTP_SERVER "pool.ntp.org"
#define TZ "EST5EDT,M3.2.0,M11.1.0" // https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv

/****************************************
 * OTA Service
 ***************************************/
#define SECRET_OTA_NAME "QUAD_SMU"
#define SECRET_OTA_PASS "QUAD_SMU"

/****************************************
 * WebSocketsServer
 ***************************************/

typedef void (*ws_conn_cb_t)();
typedef void (*ws_text_cb_t)(uint8_t *payload, size_t length);

/****************************************
 * Function Prototypes
 ***************************************/

void dns_init();
void ota_init();
void ota_process();
void wm_init();
void littlefs_init();
void littlefs_listdir(fs::FS &fs, const char * dirname, uint8_t levels);
void webserver_notfound(AsyncWebServerRequest *request);
void websocket_send(const char *message);
void webserver_init();
void websocket_set_cb(ws_conn_cb_t connected_cb, ws_conn_cb_t disconnected_cb, ws_text_cb_t text_cb);
void websocket_event(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
void websocket_init();
void websocket_process();
void debug_init();
void debug_process();
void adc_process(int64_t data);
void log_add(const std::string& message);
void log_flush();

#endif
