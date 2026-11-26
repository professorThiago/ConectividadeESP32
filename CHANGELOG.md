# Changelog

## 4.0.0

### Novidades
- **API em inglês**: todas as funções, structs e enums têm nome em inglês
  (`publish`, `onMessage`, `beginSimple`, `WiFiConfig`, `QueuePolicy::LATEST_ONLY`...).
  Os nomes em português continuam funcionando. Instância global: `conectividade` ou `connectivity`.
- **Certificados no LittleFS**: em `ConfigTLS`/`ConfigAWS`, passe o caminho (`"/certs/ca.pem"`)
  em vez do conteúdo PEM.
- **Preferences (NVS)**: `salvarCredenciaisWiFi()`, `apagarCredenciaisWiFi()`, `temCredenciaisWiFi()`
  e `salvarPreferencia()` / `lerPreferencia()` / `apagarPreferencia()` em namespace próprio.
- **Portal de configuração WiFi**: `iniciarPortalConfiguracao()` abre um AP com captive portal,
  lista as redes e salva a escolhida. HTML em `/portal/index.html` no LittleFS (personalizável),
  com página embutida de reserva. Campos extras do formulário vão para o Preferences.
- `WIFI_SALVO` / `SAVED_WIFI`: usa a rede salva; sem rede salva, abre o portal.
- `definirPortalAutomatico()`: abre o portal após N falhas seguidas de WiFi.
- `beginSomenteWiFi()`: só WiFi, sem MQTT.
- `configurarFila()`: tamanho da fila em tempo de execução.
- `configurarStatus()`: tópico de status com Last Will (online/offline, retained).
- `definirHostname()`, `ssidAtual()`, `publicar()` aceitando `String`.
- Callbacks com `std::function` — aceitam lambdas com captura.
- `#include <ConectividadeESP32.h>` (já inclui o DebugManager).

### Correções
- `#define CONNECTIVITY_FILA_*` antes do `#include` não chegava ao `.cpp` da biblioteca
  (struct com tamanhos diferentes em cada arquivo → corrupção de memória). A fila agora é
  dimensionada em tempo de execução; os `#define` valem via `build_flags`.
- A pausa após 5 falhas de MQTT não funcionava (timestamp no futuro com `uint32_t`
  fazia a subtração estourar) — a biblioteca tentava sem parar.
- Tópicos/payloads maiores que o slot eram **truncados em silêncio** (publicava no tópico errado).
  Agora a mensagem é recusada com erro. Online, publica direto sem passar pela fila.
- Mensagem maior que o buffer do PubSubClient travava a fila para sempre. Agora é recusada.
- `APENAS_ULTIMO` apagava a fila inteira; agora guarda a última mensagem **de cada tópico**.
- Ponteiros das structs eram guardados sem cópia (`String.c_str()` temporário quebrava).
- `strlen(usuario)` com `nullptr` travava o ESP32.
- Chamar `begin()` duas vezes vazava a fila.
- `WiFi.persistent(false)`: evita gravar a senha na flash a cada `begin()`.
- `mensagensNaFila()` retornava `uint8_t` (estourava com mais de 255 slots).
- Envio da fila limitado por ciclo (`CONNECTIVITY_DRENAR_POR_CICLO`) para não travar o loop.
- Exemplos não compilavam (`begin()` sem argumentos, `CONNECTIVITY_FILA_TAMANHO`,
  `main.cpp` duplicado no template). URL de instalação corrigida no README.

### Atenção ao atualizar
- QoS: o PubSubClient **publica sempre em QoS 0**. O parâmetro `qos` é mantido por compatibilidade.
- A biblioteca passa a usar WebServer, DNSServer, LittleFS e Preferences (todas do core ESP32).
