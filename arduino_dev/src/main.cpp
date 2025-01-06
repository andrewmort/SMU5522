
//TODO: must delete util directory of RemoteDebug to get it to compile
//  - need to find a workaround (fork library?)
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
#include "quad_smu.h"

#define USE_LIB_WEBSOCKET true
#define WEBSOCKET_DISABLED true
#include "RemoteDebug.h"


uint8_t count;
uint32_t millis_last;

/**********************************************************
 *
 * Callback Functions
 *
 *********************************************************/
void websocket_connected_callback() {
  smu_queue_update();
}

/**********************************************************
 *
 * Main Functions
 *
 *********************************************************/


void setup() {
  // Start wifi in station mode
  WiFi.mode(WIFI_STA);

  // Start serial port
  Serial.begin(115200);

  // Start MDNS
  dns_init();

  // Start remote debug
  debug_init();

  #if defined(ESP8266)
  // Configure timezone and ntp server
  configTime(TZ, NTP_SERVER);

  #elif  defined(ESP32)
  // Set timezone using POSIX string
  setenv("TZ", TZ, 1);  // 1 to overwrite the current value
  tzset();              // Apply the new timezone

  // Configure NTP
  configTime(0, 0, NTP_SERVER);  // Offset is handled by TZ, so set to 0
  #endif

  // Setup wifimanager
  // TODO figure out how to make this non-blocking
  wm_init();

  // Setup OTA service
  ota_init();

  // Setup littlefs
  littlefs_init();

  // Setup webserver and websocket
  webserver_init();
  websocket_init();

  // Set callbacks for websocket events
  websocket_set_cb(websocket_connected_callback,NULL,NULL);

  // Initialize smu
  smu_init();

  // Setup adc
  //TODO: test/debug stuff
  millis_last = millis();

  count = 0;
}


void loop() {
  // Handle library processing functions
  debug_process();
  ota_process();
  websocket_process();
  smu_process();

  //if(ad7177_data_ready()){
    //adc_process(ad7177_get_data());
  //}

  if (millis() - millis_last > 5000) {
    millis_last = millis();

    switch(++count) {
      case 1:
        smu_set_state(CH0, DISABLE);
        break;
      case 2:
        smu_set_dac(CH0, DAC_FV, 0);
        smu_set_state(CH0, ENABLE);
        break;
      case 3:
        smu_set_dac(CH0, DAC_FV, 1);
        break;
      case 4:
        smu_set_dac(CH0, DAC_FV, 2);
        break;
      case 5:
        smu_set_dac(CH0, DAC_FV, 3);
        break;
      default:
        count = 0;
    }

    //String message = "{\"type\":\"test\"}";
    //websocket.broadcastTXT(message);
  }

  yield();              // ESP processing time
}
