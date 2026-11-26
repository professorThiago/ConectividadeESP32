# ConectividadeESP32

Biblioteca para ESP32 que cuida de **WiFi e MQTT sem travar o `loop()`**: reconecta sozinha, guarda as mensagens enquanto está offline e tem um **portal de configuração WiFi** para escolher a rede pelo celular.

> Autor: [professorThiago](https://github.com/professorThiago) · Licença MIT
> Depende de [DebugManager](https://github.com/professorThiago/DebugManager) e PubSubClient (baixados automaticamente pelo PlatformIO).
> 🇺🇸 **English API available** — every function also has an English name. See [English API](#english-api).

---

## Funcionalidades

- WiFi e MQTT **não-bloqueantes**, com reconexão automática.
- Três modos MQTT: **SIMPLES** (1883), **TLS** (8883) e **AWS IoT Core** (mTLS).
- **Certificados e chaves no LittleFS** — basta passar o caminho do arquivo.
- **Portal de configuração WiFi** (modo AP + captive portal) com **HTML personalizável**.
- **Credenciais no Preferences (NVS)**, em namespace próprio — o resto do Preferences fica livre para o seu projeto.
- **Fila offline** com três políticas.
- **Status com Last Will** (`online`/`offline` no broker).
- Diagnóstico em português: códigos de erro do WiFi e do MQTT traduzidos, com dicas.
- API em **português e inglês**.

---

## Instalação

### PlatformIO

```ini
[env:esp32dev]
platform  = espressif32
board     = esp32dev
framework = arduino
board_build.filesystem = littlefs        ; certificados e portal

lib_deps =
    https://github.com/professorThiago/ConectividadeESP32
```

### Arduino IDE

Instale **PubSubClient** pelo gerenciador e baixe o ZIP desta biblioteca e do **DebugManager** (*Sketch → Incluir Biblioteca → Adicionar .ZIP*). Para enviar arquivos ao LittleFS, use o plugin [arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload).

---

## Início rápido

```cpp
#include <ConectividadeESP32.h>

const char* pub[] = { "sala/status", "sala/temperatura" };
const char* rec[] = { "sala/comando" };
ConfigTopicos topicos = { pub, 2, rec, 1 };

void setup() {
    configurarDebug(DEBUG_INFO);

    conectividade.registrarCallbackMensagem([](const char* topico, const String& msg) {
        debugInfo(String(topico) + " = " + msg);
    });

    conectividade.beginSimples(
        { "MinhaRede", "minha_senha" },                       // WiFi
        { "192.168.0.10", 1883, "esp32_sala", "", "" },       // broker, porta, clientId, usuário, senha
        topicos);
}

void loop() {
    conectividade.update();                                   // SEMPRE no loop()
    // ... sua lógica continua rodando, com ou sem rede
}
```

---

## Modos de conexão

```cpp
// Só WiFi, sem MQTT
conectividade.beginSomenteWiFi(wifi);

// MQTT sem criptografia — redes locais
conectividade.beginSimples(wifi, mqtt, topicos);

// MQTT com TLS — brokers públicos
conectividade.beginTLS(wifi, mqtt, { "/certs/ca.pem" }, topicos);

// AWS IoT Core — mTLS
conectividade.beginAWS(wifi,
    { "xxxx-ats.iot.us-east-1.amazonaws.com", 8883, "meu_thing",
      "/certs/AmazonRootCA1.pem", "/certs/device.crt", "/certs/private.key" },
    topicos);
```

A biblioteca **copia** todos os textos das structs, então você pode passar `String.c_str()` ou variáveis locais. Só os arrays de tópicos precisam ser globais.

---

## Certificados no LittleFS

Nos campos de certificado (`ConfigTLS` e `ConfigAWS`) você pode passar:

| Valor | Exemplo | O que acontece |
|-------|---------|----------------|
| Caminho no LittleFS | `"/certs/ca.pem"` | O arquivo é lido na hora do `begin()` |
| Conteúdo PEM | `meuCertCA` (`R"EOF(-----BEGIN...)EOF"`) | Usado direto da flash |
| Vazio / `nullptr` | `""` | TLS sem verificação (`setInsecure()`) — **só para testes** |

Estrutura do projeto:

```
meu_projeto/
├── platformio.ini          ← board_build.filesystem = littlefs
├── data/
│   ├── certs/
│   │   ├── ca.pem
│   │   ├── device.crt
│   │   └── private.key     ← NUNCA vai para o GitHub
│   └── portal/
│       └── index.html      ← opcional (portal personalizado)
└── src/
    └── main.cpp
```

Envio dos arquivos: `pio run -t uploadfs`.

> Se o seu código já usa o LittleFS (`LittleFS.begin()`), não tem problema: a biblioteca só monta se ainda não estiver montado.

---

## Portal de configuração WiFi

Abre uma rede WiFi própria do ESP32. Ao conectar o celular nela, a página abre sozinha (captive portal) — ou acesse `http://192.168.4.1`. A página lista as redes, você escolhe, digita a senha e salva. As credenciais vão para o Preferences e o ESP32 conecta.

```cpp
void setup() {
    configurarDebug(DEBUG_INFO);

    // Abre o portal sozinho se a rede salva falhar 3 vezes seguidas
    conectividade.definirPortalAutomatico(true, "Config-Sala");

    // WIFI_SALVO = usa a rede salva; se não houver, abre o portal
    conectividade.beginSimples(WIFI_SALVO, mqtt, topicos);
}

void loop() {
    conectividade.update();      // também atende o portal

    if (botaoPressionado())
        conectividade.iniciarPortalConfiguracao("Config-Sala", nullptr, 300);  // fecha após 5 min sem uso
}
```

| Função | Descrição |
|--------|-----------|
| `iniciarPortalConfiguracao(nomeAP, senhaAP, timeoutSeg)` | Abre o portal. `nomeAP = nullptr` → `ESP32-XXXX`. `senhaAP` precisa de 8+ caracteres ou `nullptr` (aberta). `timeoutSeg = 0` → não fecha sozinho. |
| `pararPortalConfiguracao()` | Fecha o portal e volta a conectar na rede salva. |
| `portalAtivo()` | `true` enquanto o portal está aberto. |
| `definirPortalAutomatico(ativo, nomeAP, senhaAP, falhas)` | Abre o portal após `falhas` tentativas seguidas sem sucesso. |
| `definirPastaPortal("/portal")` | Pasta do LittleFS com os arquivos da página. |
| `registrarCallbackPortalSalvo(cb)` | Chamado com o SSID quando o usuário salva uma rede. |

Se a senha digitada estiver errada, o portal volta a abrir depois de 3 falhas.

### Personalizando a página

A página fica em **`data/portal/index.html`** — copie a de [`examples/04_PortalWiFi/data/portal/`](examples/04_PortalWiFi/data/portal/index.html) e mude o que quiser (cores, logo, textos). Outros arquivos da pasta (`style.css`, `logo.png`...) são servidos automaticamente. Se o arquivo não existir no LittleFS, a biblioteca usa uma página embutida igual.

O HTML conversa com o firmware por estas rotas:

| Rota | Resposta |
|------|----------|
| `GET /scan` | `{"scanning":false,"networks":[{"ssid":"Casa","rssi":-52,"secure":true,"channel":6}]}` |
| `GET /scan?refresh=1` | Força uma nova varredura |
| `POST /save` | Campos `ssid` e `password` → `{"ok":true}` ou `{"ok":false,"erro":"..."}` |
| `GET /status` | `{"ap":"Config-Sala","ssid":"Casa","conectado":false,"ip":"","versao":"4.0.0"}` |

**Campos extras:** qualquer `<input name="...">` a mais no formulário (nome com até 15 caracteres) é salvo no Preferences com o mesmo nome:

```html
<input name="broker" placeholder="192.168.0.10">
```
```cpp
String broker = conectividade.lerPreferencia("broker", "192.168.0.10");
```

---

## Preferences (NVS)

A biblioteca usa o namespace `"conectividade"` (mude com `-DCONNECTIVITY_NVS_NAMESPACE=\"outro\"`) e só o abre durante cada leitura/escrita. Seu código pode usar `Preferences` com outros namespaces ao mesmo tempo.

```cpp
conectividade.salvarCredenciaisWiFi("Casa", "senha123");   // o portal faz isso por você
conectividade.temCredenciaisWiFi();                         // bool
conectividade.apagarCredenciaisWiFi();                      // "esquecer rede"

// Valores do seu projeto, no mesmo namespace
conectividade.salvarPreferencia("intervalo", "30");
int intervalo = conectividade.lerPreferencia("intervalo", "10").toInt();
conectividade.apagarPreferencia("intervalo");
```

Chaves reservadas: `wifi_ssid` e `wifi_pass`.

---

## Publicação

```cpp
conectividade.publicar(0, "online");                 // por índice do array de tópicos
conectividade.publicar("sala/luz", "on");            // por nome
conectividade.publicar(1, payloadString);            // aceita String
conectividade.publicar(0, "ping", false);            // false = não enfileira se offline
conectividade.publicarQoS("sala/estado", "1", 0, true);   // retained
```

Retorna `true` se publicou na hora e `false` se foi para a fila (ou foi descartada).

> O PubSubClient **publica sempre em QoS 0**. QoS 1 vale só para as inscrições (a biblioteca se inscreve com QoS 1).

### Status com Last Will

```cpp
conectividade.configurarStatus("sala/status");             // "online" / "offline"
conectividade.configurarStatus("sala/status", "1", "0");   // textos personalizados
```

Ao conectar, publica `online` (retained). Se o ESP32 desligar ou perder a rede, o próprio broker publica `offline`.

---

## Fila offline

Mensagens publicadas sem conexão vão para a fila e são enviadas na ordem quando o MQTT volta.

```cpp
// Antes do begin():
conectividade.configurarFila(20, 64, 256);   // slots, bytes do tópico, bytes do payload
conectividade.definirPoliticaFila(PoliticaFila::APENAS_ULTIMO);
```

| Política | Comportamento | Quando usar |
|----------|---------------|-------------|
| `FILA_COMPLETA` (padrão) | Guarda tudo; cheia → descarta a mais antiga | Eventos, logs, alarmes |
| `APENAS_ULTIMO` | Guarda só a última mensagem **de cada tópico** | Estado atual (status, modo, posição) |
| `DESCARTAR` | Não guarda nada offline | Leituras contínuas de sensor |

Tópico ou payload maior que o slot é **recusado com erro** no log (antes era cortado sem aviso). Mensagens maiores que 256 bytes precisam de `configurarBufferMQTT()`:

```cpp
conectividade.configurarBufferMQTT(1024);
```

### PSRAM (ESP32-S3 N16R8 e similares)

```ini
board_build.arduino.memory_type = qio_opi
build_flags =
    -DBOARD_HAS_PSRAM
    -DCONNECTIVITY_USAR_PSRAM=1
```
```cpp
conectividade.configurarFila(200);   // ~63 KB na PSRAM
```

### Ajustes de compilação (`build_flags`)

| Define | Padrão | Descrição |
|--------|--------|-----------|
| `CONNECTIVITY_FILA_SLOTS` | `10` | Slots padrão da fila |
| `CONNECTIVITY_FILA_TOPICO_MAX` | `64` | Bytes por tópico |
| `CONNECTIVITY_FILA_PAYLOAD_MAX` | `256` | Bytes por payload |
| `CONNECTIVITY_DRENAR_POR_CICLO` | `10` | Mensagens enviadas da fila por `update()` |
| `CONNECTIVITY_USAR_PSRAM` | `0` | Fila na PSRAM |
| `CONNECTIVITY_NVS_NAMESPACE` | `"conectividade"` | Namespace do Preferences |
| `CONNECTIVITY_PORTAL_PASTA` | `"/portal"` | Pasta da página do portal |
| `CONNECTIVITY_FALHAS_PARA_PORTAL` | `3` | Falhas antes do portal automático |

> ⚠ Use `build_flags`. Um `#define` antes do `#include` **não** chega ao código da biblioteca.

---

## Estado e callbacks

```cpp
conectividade.wifiConectado();     // bool
conectividade.mqttConectado();     // bool
conectividade.mensagensNaFila();   // uint16_t
conectividade.ssidAtual();         // String
conectividade.definirHostname("esp32-sala");   // antes do begin

conectividade.registrarCallbackWiFiConectado(   []() { ... });
conectividade.registrarCallbackWiFiDesconectado([]() { ... });
conectividade.registrarCallbackMQTTConectado(   []() { ... });
conectividade.registrarCallbackMQTTDesconectado([]() { ... });
conectividade.registrarCallbackMensagem([](const char* topico, const String& msg) { ... });
conectividade.registrarCallbackPortalSalvo([](const String& ssid) { ... });
```

Os callbacks aceitam lambdas com captura (`[&]`).

---

## English API

Same library, same object (`connectivity` is the same as `conectividade`), English names:

```cpp
#include <ConectividadeESP32.h>

const char* pubTopics[] = { "lab/status" };
const char* subTopics[] = { "lab/cmd" };
TopicsConfig topics = { pubTopics, 1, subTopics, 1 };

void setup() {
    configurarDebug(DEBUG_INFO);
    connectivity.setAutoConfigPortal(true, "ESP32-Setup");
    connectivity.onMessage([](const char* topic, const String& msg) { /* ... */ });
    connectivity.beginTLS(SAVED_WIFI,
                          { "broker.hivemq.com", 8883, "esp32_lab", "", "" },
                          { "/certs/ca.pem" },
                          topics);
}

void loop() {
    connectivity.update();
    connectivity.publish(0, "online");
}
```

| Português | English |
|-----------|---------|
| `beginSomenteWiFi` / `beginSimples` | `beginWiFiOnly` / `beginSimple` |
| `beginTLS` / `beginAWS` / `update` | *(same)* |
| `publicar` / `publicarQoS` | `publish` / `publishQoS` |
| `configurarFila` / `definirPoliticaFila` | `setQueueSize` / `setQueuePolicy` |
| `configurarBufferMQTT` / `definirQoSPadrao` | `setMQTTBufferSize` / `setDefaultQoS` |
| `configurarStatus` / `definirHostname` | `setStatusTopic` / `setHostname` |
| `wifiConectado` / `mqttConectado` | `isWiFiConnected` / `isMQTTConnected` |
| `mensagensNaFila` / `ssidAtual` | `queuedMessages` / `currentSSID` |
| `topicoPublicacao` / `topicoRecebimento` | `publishTopic` / `subscribeTopic` |
| `registrarCallbackMensagem` | `onMessage` |
| `registrarCallbackWiFiConectado` / `...Desconectado` | `onWiFiConnected` / `onWiFiDisconnected` |
| `registrarCallbackMQTTConectado` / `...Desconectado` | `onMQTTConnected` / `onMQTTDisconnected` |
| `registrarCallbackPortalSalvo` | `onPortalSaved` |
| `iniciarPortalConfiguracao` / `pararPortalConfiguracao` | `startConfigPortal` / `stopConfigPortal` |
| `portalAtivo` / `definirPortalAutomatico` / `definirPastaPortal` | `isConfigPortalActive` / `setAutoConfigPortal` / `setPortalFolder` |
| `salvarCredenciaisWiFi` / `apagarCredenciaisWiFi` / `temCredenciaisWiFi` | `saveWiFiCredentials` / `clearWiFiCredentials` / `hasWiFiCredentials` |
| `salvarPreferencia` / `lerPreferencia` / `apagarPreferencia` | `savePreference` / `readPreference` / `removePreference` |
| `iniciarLittleFS` / `lerArquivo` | `beginLittleFS` / `readFile` |
| `ConfigWiFi` `ConfigMQTT` `ConfigTLS` `ConfigAWS` `ConfigTopicos` | `WiFiConfig` `MQTTConfig` `TLSConfig` `AWSConfig` `TopicsConfig` |
| `ModoConexao::SIMPLES` / `AWS_IOT` | `ConnectionMode::PLAIN` / `AWS` |
| `PoliticaFila::FILA_COMPLETA` / `APENAS_ULTIMO` / `DESCARTAR` | `QueuePolicy::FULL_QUEUE` / `LATEST_ONLY` / `DROP` |
| `WIFI_SALVO` | `SAVED_WIFI` |

Struct fields also have English names: `password` (= `senha`), `port` (= `porta`), `username` (= `usuario`), `caCert` (= `certificadoCA`), `publish` / `publishCount` / `subscribe` / `subscribeCount`.

---

## Debug

```cpp
configurarDebug(DEBUG_INFO, 4);   // nível INFO; GPIO 4 no GND ao ligar → DEBUG_TUDO
```

| Nível | Constante | O que exibe |
|-------|-----------|-------------|
| 0 | `DEBUG_NENHUM` | Nada |
| 1 | `DEBUG_ERRO` | Falhas críticas |
| 2 | `DEBUG_AVISO` | + Reconexões e timeouts |
| 3 | `DEBUG_INFO` | + Fluxo normal |
| 4 | `DEBUG_VERBOSE` | + Tentativas e estados internos |
| 5 | `DEBUG_TUDO` | + Payloads |

---

## Exemplos

| Exemplo | Descrição |
|---------|-----------|
| `00_Template` | Ponto de partida com `secrets.h` / `secrets.cpp` |
| `01_WiFiBasico` | Só WiFi, não-bloqueante |
| `02_MQTTBasico` | WiFi + MQTT com callbacks e Last Will |
| `03_FilaOffline` | Enfileiramento e envio ao reconectar |
| `04_PortalWiFi` | Portal de configuração com HTML no LittleFS |
| `05_EnglishAPI_LittleFS` | API em inglês + certificados TLS no LittleFS |

---

## Como funciona

```
WiFi:  DESCONECTADO ──begin()──> CONECTANDO ──OK──> CONECTADO
            ^                        │                   │
            └──── nova tentativa 5s ─┘ timeout 15s     queda
                         │
                 N falhas + portal automático → PORTAL (AP)

MQTT:  DESCONECTADO ──WiFi OK──> CONECTANDO ──OK──> CONECTADO
            ^                        │                   │
            └── 5s entre tentativas ─┘                broker caiu
                5 falhas → pausa de 30s
```

> `connect()` do PubSubClient bloqueia durante o handshake (1–3 s em TLS). A biblioteca limita esse tempo (handshake 8 s, socket 5 s), mas um loop com tempo crítico deve considerar isso.

---

## Licença

MIT © 2026 [professorThiago](https://github.com/professorThiago)
