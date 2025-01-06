#ifndef EVAL_H
#define EVAL_H

/**********************************************************
 *
 * MDNS Functions
 *
 *********************************************************/

// Setup multicast DNS name
void dns_init();

/**********************************************************
 *
 * OTA Functions
 *
 *********************************************************/
void ota_init();

/**********************************************************
 *
 * Little FS Functions
 *
 *********************************************************/

void littlefs_init();
void littlefs_listdir(fs::FS &fs, const char * dirname, uint8_t levels);

/**********************************************************
 *
 * Websever Functions
 *
 *********************************************************/

void webserver_notfound(AsyncWebServerRequest *request);
void webserver_init();

/**********************************************************
 *
 * Websocket Functions
 *
 *********************************************************/

// WebSocket event handler
void websocket_event(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
void websocket_init();

/**********************************************************
 *
 * RemoteDebug Functions
 *
 *********************************************************/
void debug_init();
void debug_process();

#endif
