#include <Arduino.h>
#include <WiFi.h>

#include "hal/uart_cpx_bridge.hpp"
#include "net/gateway_web_server.hpp"
#include "wifi_secrets.h"

static mcshal::UartCpxBridge g_cpx_bridge(Serial1, CPX_UART_RX_PIN, CPX_UART_TX_PIN, CPX_UART_BAUD);
static mcshal::GatewayWebServer g_web_server(g_cpx_bridge, 80);

static void setupWifi() {
    if ( strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        Serial.printf("Connecting to Wi-Fi '%s'...", WIFI_SSID);
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - start) < 10000) {
            delay(250);
            Serial.print(".");
        }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("Connected! Open http://%s/\n", WiFi.localIP().toString().c_str());
            return;
        }
    }

    WiFi.mode(WIFI_AP);
    WiFi.softAP(SOFTAP_SSID);
    Serial.printf("SoftAP '%s' started! Open http://%s/\n",
                  SOFTAP_SSID, WiFi.softAPIP().toString().c_str());
}

void setup() {
    Serial.begin(115200);
    g_cpx_bridge.begin();
    setupWifi();
    g_web_server.begin();
}

void loop() {
    g_cpx_bridge.poll(millis());
    g_web_server.handleClient();
}
