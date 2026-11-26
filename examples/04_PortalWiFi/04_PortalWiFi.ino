/**
 * @file 04_PortalWiFi.ino
 * @brief ConectividadeESP32 — Exemplo 4: portal de configuração WiFi (modo AP).
 *
 * Fluxo:
 *  1. Sem rede salva, o ESP32 cria a rede "Config-Sala" e abre o portal.
 *  2. Conecte o celular nela — a página abre sozinha (ou acesse http://192.168.4.1).
 *  3. Escolha a rede, digite a senha e salve. As credenciais vão para o
 *     Preferences (NVS) e o ESP32 conecta.
 *  4. Se a senha estiver errada (3 falhas), o portal volta.
 *  5. Segure o botão BOOT (GPIO 0) por 3 s para abrir o portal de novo.
 *
 * Página personalizável: data/portal/index.html (vai para o LittleFS).
 *   PlatformIO:  coloque a pasta data/ na raiz do projeto e rode
 *                pio run -t uploadfs
 *                (no platformio.ini: board_build.filesystem = littlefs)
 *   Arduino IDE: plugin "arduino-littlefs-upload" → Ctrl+Shift+P →
 *                "Upload LittleFS to Pico/ESP8266/ESP32"
 * Sem o arquivo, a biblioteca usa uma página embutida igual a esta.
 *
 * @author  professorThiago (https://github.com/professorThiago)
 * @license MIT
 */

#include <ConectividadeESP32.h>

const int PINO_BOTAO = 0;          // BOOT
uint32_t  botaoDesde = 0;

void setup() {
    configurarDebug(DEBUG_INFO);
    pinMode(PINO_BOTAO, INPUT_PULLUP);

    // Abre o portal sozinho se a rede salva falhar 3 vezes seguidas
    conectividade.definirPortalAutomatico(true, "Config-Sala", nullptr, 3);

    conectividade.registrarCallbackPortalSalvo([](const String& ssid) {
        debugInfo("Rede escolhida no portal: " + ssid);
        // Campo extra do HTML (ex.: <input name="broker">):
        // String broker = conectividade.lerPreferencia("broker");
    });

    conectividade.registrarCallbackWiFiConectado([]() {
        debugInfo("Conectado a '" + conectividade.ssidAtual() + "' — IP " + WiFi.localIP().toString());
    });

    // WIFI_SALVO = usa a rede salva; se não houver nenhuma, abre o portal
    conectividade.beginSomenteWiFi(WIFI_SALVO);
}

void loop() {
    conectividade.update();   // também atende o portal quando ele está aberto

    // Botão BOOT pressionado por 3 s → abre o portal
    if (digitalRead(PINO_BOTAO) == LOW) {
        if (botaoDesde == 0) botaoDesde = millis();
        if (millis() - botaoDesde > 3000 && !conectividade.portalAtivo()) {
            conectividade.iniciarPortalConfiguracao("Config-Sala", nullptr, 300);   // fecha após 5 min sem uso
        }
    } else {
        botaoDesde = 0;
    }
}
