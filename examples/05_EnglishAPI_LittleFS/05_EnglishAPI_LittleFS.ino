/**
 * @file 05_EnglishAPI_LittleFS.ino
 * @brief ConectividadeESP32 — Example 5: English API + TLS certificates in LittleFS.
 *
 * Every function has an English name that calls the same code as the
 * Portuguese one (publish = publicar, onMessage = registrarCallbackMensagem...).
 *
 * Certificates are read from LittleFS — just pass the path.
 * Put your files in data/certs/ and upload them:
 *   PlatformIO:  pio run -t uploadfs   (board_build.filesystem = littlefs)
 *   Arduino IDE: arduino-littlefs-upload plugin
 *
 * For AWS IoT Core:
 *   connectivity.beginAWS(SAVED_WIFI,
 *       { "xxxx-ats.iot.us-east-1.amazonaws.com", 8883, "my_thing",
 *         "/certs/AmazonRootCA1.pem", "/certs/device.crt", "/certs/private.key" },
 *       topics);
 *
 * @author  professorThiago (https://github.com/professorThiago)
 * @license MIT
 */

#include <ConectividadeESP32.h>

const char* PUBLISH_TOPICS[]   = { "lab/esp32/status", "lab/esp32/data" };
const char* SUBSCRIBE_TOPICS[] = { "lab/esp32/cmd" };
TopicsConfig topics = { PUBLISH_TOPICS, 2, SUBSCRIBE_TOPICS, 1 };

uint32_t lastPublish = 0;

void setup() {
    configurarDebug(DEBUG_INFO);

    connectivity.setHostname("esp32-lab");
    connectivity.setQueuePolicy(QueuePolicy::LATEST_ONLY);
    connectivity.setStatusTopic("lab/esp32/status");          // LWT: online/offline
    connectivity.setAutoConfigPortal(true, "ESP32-Setup");    // portal if WiFi keeps failing

    connectivity.onMessage([](const char* topic, const String& msg) {
        debugInfo(String("Received ") + topic + ": " + msg);
    });
    connectivity.onMQTTConnected([]() { debugInfo("MQTT up!"); });

    connectivity.beginTLS(
        SAVED_WIFI,                                             // from the portal / NVS
        { "broker.hivemq.com", 8883, "esp32_lab_change_me", "", "" },
        { "/certs/ca.pem" },                                    // file in LittleFS
        topics);
}

void loop() {
    connectivity.update();

    if (millis() - lastPublish >= 10000) {
        lastPublish = millis();
        connectivity.publish(1, "{\"uptime\":" + String(millis() / 1000) + "}");
    }
}
