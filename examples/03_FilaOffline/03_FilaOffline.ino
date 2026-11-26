/**
 * @file 03_FilaOffline.ino
 * @brief ConectividadeESP32 — Exemplo 3: fila de mensagens offline.
 *
 * Mensagens publicadas sem MQTT vão para a fila e são enviadas ao reconectar.
 * Para testar: desligue o roteador/broker, espere algumas mensagens
 * enfileirarem e religue.
 *
 * @author  professorThiago (https://github.com/professorThiago)
 * @license MIT
 */

#include <ConectividadeESP32.h>

const char* TOPICOS_PUB[] = { "aula/esp32/log" };
ConfigTopicos topicos = { TOPICOS_PUB, 1, nullptr, 0 };

const uint16_t SLOTS_FILA = 20;

uint32_t ultimaPublicacao  = 0;
uint32_t ultimoStatus      = 0;
uint32_t contadorMensagens = 0;

void setup() {
    configurarDebug(DEBUG_INFO);

    // Tamanho da fila em tempo de execução (antes do begin)
    conectividade.configurarFila(SLOTS_FILA, 64, 128);
    conectividade.definirPoliticaFila(PoliticaFila::FILA_COMPLETA);

    conectividade.registrarCallbackMQTTConectado([]() {
        debugInfo("MQTT conectado! A fila sera enviada automaticamente...");
    });

    conectividade.beginSimples(
        { "NOME_DA_REDE", "SENHA_DA_REDE" },
        { "test.mosquitto.org", 1883, "esp32_fila_trocar_id", "", "" },
        topicos);
}

void loop() {
    conectividade.update();

    if (millis() - ultimaPublicacao >= 3000) {
        ultimaPublicacao = millis();
        contadorMensagens++;

        String payload = "{\"seq\":" + String(contadorMensagens) + ",\"ts\":" + String(millis()) + "}";

        if (conectividade.publicar(0, payload))
            debugInfo("Mensagem " + String(contadorMensagens) + " enviada.");
        else
            debugInfo("Mensagem " + String(contadorMensagens) + " na fila: " +
                      String(conectividade.mensagensNaFila()) + "/" + String(SLOTS_FILA));
    }

    if (millis() - ultimoStatus >= 5000) {
        ultimoStatus = millis();
        debugInfo(String("WiFi: ") + (conectividade.wifiConectado() ? "OK" : "OFFLINE") +
                  " | MQTT: " + (conectividade.mqttConectado() ? "OK" : "OFFLINE") +
                  " | Fila: " + String(conectividade.mensagensNaFila()));
    }
}
