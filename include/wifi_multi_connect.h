// Compile-time multi-AP WiFi connect. Lists up to 5 known SSID/password
// pairs (the unsuffixed WIFI_SSID + four numbered slots) and lets
// WiFiMulti scan + pick the strongest available network. Shared by the
// camera, LED placar, and HMI firmwares so all three behave identically
// when carried between locations.
//
// Usage:
//
//   #include "wifi_multi_connect.h"
//   static WiFiMulti wifiMulti;     // file-scope
//   ...
//   void setup() {
//     ...
//     connectWiFiMulti(wifiMulti);
//     ...
//   }
//
// To add a network, edit .env:
//
//   WIFI_SSID=primary-net           # slot 0 (original schema)
//   WIFI_PASSWORD=...
//   WIFI_SSID_1=secondary-net       # additional slots
//   WIFI_PASSWORD_1=...
//   WIFI_SSID_2=...                 # up to _4
//   WIFI_PASSWORD_2=...
//
// Only the slots actually defined are added; missing slots are ignored.
// Reflash to pick up changes.
//
// NB: WiFiMulti picks based on signal strength, not slot order — slots
// are just convenient buckets, not a priority list.
#pragma once

#include <WiFi.h>
#include <WiFiMulti.h>

inline int connectWiFiMulti(WiFiMulti& wifiMulti, int maxRetries = 30) {
    int slots = 0;
#ifdef WIFI_SSID
    wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
    slots++;
#endif
#ifdef WIFI_SSID_1
    wifiMulti.addAP(WIFI_SSID_1, WIFI_PASSWORD_1);
    slots++;
#endif
#ifdef WIFI_SSID_2
    wifiMulti.addAP(WIFI_SSID_2, WIFI_PASSWORD_2);
    slots++;
#endif
#ifdef WIFI_SSID_3
    wifiMulti.addAP(WIFI_SSID_3, WIFI_PASSWORD_3);
    slots++;
#endif
#ifdef WIFI_SSID_4
    wifiMulti.addAP(WIFI_SSID_4, WIFI_PASSWORD_4);
    slots++;
#endif

    Serial.printf("[wifi] %d known networks; scanning...", slots);

    int retries = 0;
    while (wifiMulti.run() != WL_CONNECTED && retries < maxRetries) {
        delay(500);
        Serial.print(".");
        retries++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[wifi] connected to '%s', IP=%s, RSSI=%d dBm\n",
                      WiFi.SSID().c_str(),
                      WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());
        return 1;
    }
    Serial.println("[wifi] FAILED — will keep retrying in loop()");
    return 0;
}
