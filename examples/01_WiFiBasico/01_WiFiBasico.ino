/**
 * @file 01_WiFiBasico.ino
 * @brief ConectividadeESP32 — Exemplo 1: WiFi não-bloqueante (sem MQTT).
 *
 * O setup() retorna na hora, mesmo sem WiFi. Observe no Monitor Serial que o
 * contador continua subindo enquanto a conexão acontece em segundo plano.
 *
 * @author  professorThiago (https://github.com/professorThiago)
 * @license MIT
 */

#include <ConectividadeESP32.h>

uint32_t ultimoLog = 0;
uint32_t contador  = 0;

void setup() {
    configurarDebug(DEBUG_INFO);

    conectividade.registrarCallbackWiFiConectado([]() {
        debugInfo(">>> WiFi conectou! IP = " + WiFi.localIP().toString());
    });
    conectividade.registrarCallbackWiFiDesconectado([]() {
        debugErro(">>> WiFi caiu!");
    });

    conectividade.beginSomenteWiFi({ "NOME_DA_REDE", "SENHA_DA_REDE" });
    // Ou use as credenciais salvas pelo portal (exemplo 04):
    // conectividade.beginSomenteWiFi(WIFI_SALVO);

    debugInfo("setup() concluido. Loop iniciando...");
}

void loop() {
    conectividade.update();   // avança a máquina de estados — não bloqueia

    if (millis() - ultimoLog >= 2000) {
        ultimoLog = millis();
        contador++;
        debugInfo("[" + String(contador) + "] WiFi: " +
                  (conectividade.wifiConectado() ? "CONECTADO" : "aguardando..."));
    }
}
