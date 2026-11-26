/**
 * @file 02_MQTTBasico.ino
 * @brief ConectividadeESP32 — Exemplo 2: WiFi + MQTT com callbacks.
 *
 * Usa o broker público test.mosquitto.org (porta 1883, sem criptografia).
 * Teste no PC:
 *   mosquitto_sub -h test.mosquitto.org -t "aula/esp32/#" -v
 *   mosquitto_pub -h test.mosquitto.org -t "aula/esp32/comando" -m ligar
 *
 * @author  professorThiago (https://github.com/professorThiago)
 * @license MIT
 */

#include <ConectividadeESP32.h>

const char* TOPICOS_PUB[] = { "aula/esp32/status", "aula/esp32/dados" };
const char* TOPICOS_REC[] = { "aula/esp32/comando" };
ConfigTopicos topicos = { TOPICOS_PUB, 2, TOPICOS_REC, 1 };

uint32_t ultimaPublicacao = 0;
uint32_t contador = 0;

void aoReceberMensagem(const char* topico, const String& mensagem) {
    debugInfo(">>> " + String(topico) + " = " + mensagem);
    if (mensagem == "ligar")         debugInfo("    Acao: ligar dispositivo");
    else if (mensagem == "desligar") debugInfo("    Acao: desligar dispositivo");
}

void setup() {
    configurarDebug(DEBUG_INFO);

    conectividade.registrarCallbackMensagem(aoReceberMensagem);
    conectividade.registrarCallbackMQTTConectado([]() { debugInfo(">>> MQTT conectado!"); });

    // Status com Last Will: "online" ao conectar, "offline" se o ESP32 cair
    conectividade.configurarStatus("aula/esp32/status");

    conectividade.beginSimples(
        { "NOME_DA_REDE", "SENHA_DA_REDE" },
        { "test.mosquitto.org", 1883, "esp32_aula_trocar_id", "", "" },
        topicos);
}

void loop() {
    conectividade.update();

    if (millis() - ultimaPublicacao >= 10000) {
        ultimaPublicacao = millis();
        contador++;
        String payload = "{\"contador\":" + String(contador) +
                         ",\"uptime\":" + String(millis() / 1000) + "}";
        conectividade.publicar(1, payload);   // aceita String direto
    }
}
