/**
 * @file ESP32Connectivity.h
 * @brief Gerenciamento não-bloqueante de WiFi e MQTT para ESP32.
 *        Non-blocking WiFi + MQTT manager for ESP32.
 *
 * @details
 * - WiFi e MQTT não-bloqueantes, com reconexão automática.
 * - Modos MQTT: SIMPLES (1883), TLS (8883) e AWS IoT Core (mTLS).
 * - Fila offline com três políticas.
 * - Certificados/chaves lidos do **LittleFS** (basta passar o caminho, ex.: "/certs/ca.pem").
 * - Credenciais WiFi salvas no **Preferences (NVS)**, em namespace próprio.
 * - **Portal de configuração** (modo AP + captive portal) com HTML personalizável no LittleFS.
 * - API em **português e inglês** — os dois nomes chamam a mesma função.
 *
 * @par Uso básico (PT)
 * @code
 * #include <ConectividadeESP32.h>
 *
 * const char* pub[] = { "sala/status" };
 * const char* rec[] = { "sala/comando" };
 * ConfigTopicos topicos = { pub, 1, rec, 1 };
 *
 * void setup() {
 *     configurarDebug(DEBUG_INFO);
 *     conectividade.beginSimples({ "MinhaRede", "minha_senha" },
 *                                { "192.168.0.10", 1883, "esp32_sala", "", "" },
 *                                topicos);
 * }
 * void loop() { conectividade.update(); }
 * @endcode
 *
 * @par Basic usage (EN)
 * @code
 * void setup() {
 *     connectivity.beginSimple(SAVED_WIFI,                       // credentials from NVS / portal
 *                              { "192.168.0.10", 1883, "esp32_room", "", "" },
 *                              topics);
 * }
 * void loop() { connectivity.update(); }
 * @endcode
 *
 * @author  professorThiago (https://github.com/professorThiago)
 * @version 4.0.0
 * @license MIT
 */

#ifndef ESP32_CONNECTIVITY_H
#define ESP32_CONNECTIVITY_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <functional>

// Declarações antecipadas — os headers só são incluídos no .cpp
class WebServer;
class DNSServer;

#define CONNECTIVITY_VERSAO "4.0.0"
#define CONNECTIVITY_VERSION CONNECTIVITY_VERSAO

// ---------------------------------------------------------------------------
// Valores padrão (altere via build_flags no platformio.ini, ex.:
//   build_flags = -DCONNECTIVITY_FILA_SLOTS=50
// ou em tempo de execução com configurarFila() / setQueueSize()).
//
// ⚠ Um #define antes do #include NÃO funciona para a biblioteca: o .cpp dela
//   é compilado separadamente e não enxerga o seu #define.
// ---------------------------------------------------------------------------

/** @brief Slots na fila offline (número de mensagens). */
#ifndef CONNECTIVITY_FILA_SLOTS
  #define CONNECTIVITY_FILA_SLOTS 10
#endif

/** @brief Tamanho máximo do tópico em cada slot (bytes, incluindo o '\\0'). */
#ifndef CONNECTIVITY_FILA_TOPICO_MAX
  #define CONNECTIVITY_FILA_TOPICO_MAX 64
#endif

/** @brief Tamanho máximo do payload em cada slot (bytes, incluindo o '\\0'). */
#ifndef CONNECTIVITY_FILA_PAYLOAD_MAX
  #define CONNECTIVITY_FILA_PAYLOAD_MAX 256
#endif

/** @brief Máximo de mensagens da fila enviadas por chamada de update() (evita travar o loop). */
#ifndef CONNECTIVITY_DRENAR_POR_CICLO
  #define CONNECTIVITY_DRENAR_POR_CICLO 10
#endif

/** @brief 1 = aloca a fila na PSRAM (quando disponível). Veja o README. */
#ifndef CONNECTIVITY_USAR_PSRAM
  #define CONNECTIVITY_USAR_PSRAM 0
#endif

/** @brief Namespace do Preferences (NVS) usado pela biblioteca (máx. 15 caracteres). */
#ifndef CONNECTIVITY_NVS_NAMESPACE
  #define CONNECTIVITY_NVS_NAMESPACE "conectividade"
#endif

/** @brief Pasta no LittleFS com os arquivos do portal (index.html, css, js, imagens). */
#ifndef CONNECTIVITY_PORTAL_PASTA
  #define CONNECTIVITY_PORTAL_PASTA "/portal"
#endif

/** @brief Falhas seguidas de conexão WiFi antes de abrir o portal automático. */
#ifndef CONNECTIVITY_FALHAS_PARA_PORTAL
  #define CONNECTIVITY_FALHAS_PARA_PORTAL 3
#endif

// ---------------------------------------------------------------------------
// Enums (valores em PT e EN apontam para o mesmo número)
// ---------------------------------------------------------------------------

/** @brief Protocolo / nível de segurança da conexão MQTT. */
enum class ModoConexao : uint8_t {
    SIMPLES = 0,          ///< MQTT sem criptografia (porta 1883 — só redes locais confiáveis)
    TLS     = 1,          ///< MQTT com TLS (porta 8883 — brokers públicos)
    AWS_IOT = 2,          ///< AWS IoT Core com mTLS (porta 8883)

    PLAIN   = SIMPLES,    ///< EN: plain MQTT
    AWS     = AWS_IOT     ///< EN: AWS IoT Core
};
using ConnectionMode = ModoConexao;

/**
 * @brief Comportamento da fila quando o MQTT está offline.
 *
 * | Política | Quando usar |
 * |----------|-------------|
 * | `FILA_COMPLETA` / `FULL_QUEUE`  | Eventos, logs, alarmes — tudo deve chegar |
 * | `APENAS_ULTIMO` / `LATEST_ONLY` | Estado atual — guarda só a última mensagem **de cada tópico** |
 * | `DESCARTAR`     / `DROP`        | Leituras contínuas — valor velho não serve |
 */
enum class PoliticaFila : uint8_t {
    FILA_COMPLETA = 0,
    APENAS_ULTIMO = 1,
    DESCARTAR     = 2,

    FULL_QUEUE  = FILA_COMPLETA,
    LATEST_ONLY = APENAS_ULTIMO,
    DROP        = DESCARTAR
};
using QueuePolicy = PoliticaFila;

// ---------------------------------------------------------------------------
// Structs de configuração
//
// Os campos com dois nomes (ex.: senha / password) são o MESMO campo — use o
// que preferir. A inicialização por chaves continua igual: { "rede", "senha" }.
// A biblioteca copia os textos internamente, então pode passar String.c_str()
// ou variáveis locais sem risco.
// ---------------------------------------------------------------------------

/** @brief Credenciais WiFi. Use `WIFI_SALVO` / `SAVED_WIFI` para usar as salvas no NVS. */
struct ConfigWiFi {
    const char* ssid;                                  ///< Nome da rede
    union { const char* senha; const char* password; };///< Senha da rede
};
using WiFiConfig = ConfigWiFi;

/** @brief Usa as credenciais salvas (portal ou salvarCredenciaisWiFi). Sem credenciais → abre o portal. */
#define WIFI_SALVO  (ConfigWiFi{ nullptr, { nullptr } })
#define SAVED_WIFI  WIFI_SALVO

/** @brief Broker MQTT (modos SIMPLES e TLS). */
struct ConfigMQTT {
    const char* broker;                                   ///< Hostname ou IP
    union { int porta; int port; };                       ///< 1883 sem TLS, 8883 com TLS
    const char* clientId;                                 ///< ID único do dispositivo
    union { const char* usuario; const char* username; }; ///< "" ou nullptr = anônimo
    union { const char* senha;   const char* password; }; ///< "" ou nullptr = anônimo
};
using MQTTConfig = ConfigMQTT;

/**
 * @brief Certificado CA para o modo TLS.
 *
 * Aceita **o conteúdo PEM** (começa com "-----BEGIN") **ou um caminho no
 * LittleFS** (começa com "/", ex.: "/certs/ca.pem").
 * Vazio ou nullptr → setInsecure() (apenas testes!).
 */
struct ConfigTLS {
    union { const char* certificadoCA; const char* caCert; };
};
using TLSConfig = ConfigTLS;

/**
 * @brief AWS IoT Core (modo AWS_IOT).
 * Cada certificado aceita conteúdo PEM **ou** caminho no LittleFS.
 */
struct ConfigAWS {
    const char* endpoint;                         ///< xxxx-ats.iot.<região>.amazonaws.com
    union { int porta; int port; };               ///< Normalmente 8883
    const char* clientId;                         ///< Thing Name
    const char* certCA;                           ///< AmazonRootCA1.pem  (ou "/certs/AmazonRootCA1.pem")
    const char* certCRT;                          ///< Certificado do dispositivo (ou "/certs/device.crt")
    const char* certPrivate;                      ///< Chave privada        (ou "/certs/private.key")
};
using AWSConfig = ConfigAWS;

/**
 * @brief Tópicos MQTT.
 * @warning Os arrays precisam existir durante todo o programa (globais ou static).
 */
struct ConfigTopicos {
    union { const char** publicar; const char** publish;        };
    union { int nPublicar;         int publishCount;            };
    union { const char** receber;  const char** subscribe;      };
    union { int nReceber;          int subscribeCount;          };
};
using TopicsConfig = ConfigTopicos;

// ---------------------------------------------------------------------------
// Callbacks — aceitam funções comuns e lambdas (inclusive com captura)
// ---------------------------------------------------------------------------

using CallbackMensagemMQTT = std::function<void(const char* topico, const String& mensagem)>;
using CallbackWiFi         = std::function<void()>;
using CallbackMQTT         = std::function<void()>;
using CallbackPortalSalvo  = std::function<void(const String& ssid)>;

using MessageCallback      = CallbackMensagemMQTT;
using EventCallback        = std::function<void()>;
using PortalSavedCallback  = CallbackPortalSalvo;

// ---------------------------------------------------------------------------
// Classe principal
// ---------------------------------------------------------------------------

class ESP32Connectivity
{
public:
    ESP32Connectivity() = default;
    ~ESP32Connectivity();
    ESP32Connectivity(const ESP32Connectivity&) = delete;
    ESP32Connectivity& operator=(const ESP32Connectivity&) = delete;

    // =======================================================================
    // Inicialização
    // =======================================================================

    /** @brief Só WiFi, sem MQTT. */
    void beginSomenteWiFi(const ConfigWiFi& wifi);

    /** @brief MQTT sem criptografia (porta 1883). */
    void beginSimples(const ConfigWiFi& wifi, const ConfigMQTT& mqtt, const ConfigTopicos& topicos);

    /** @brief MQTT com TLS (porta 8883). `tls` aceita PEM ou caminho no LittleFS. */
    void beginTLS(const ConfigWiFi& wifi, const ConfigMQTT& mqtt,
                  const ConfigTLS& tls, const ConfigTopicos& topicos);

    /** @brief AWS IoT Core com mTLS. Certificados aceitam PEM ou caminho no LittleFS. */
    void beginAWS(const ConfigWiFi& wifi, const ConfigAWS& aws, const ConfigTopicos& topicos);

    /** @brief Avança as máquinas de estado, o portal e a fila. **Chame em todo loop().** */
    void update();

    /** @brief Nome do dispositivo na rede (chame antes do begin). */
    void definirHostname(const char* nome);

    // =======================================================================
    // Fila e MQTT (chame antes do begin)
    // =======================================================================

    /**
     * @brief Tamanho da fila offline em tempo de execução.
     * @param slots       Número de mensagens.
     * @param topicoMax   Bytes por tópico (inclui '\\0').
     * @param payloadMax  Bytes por payload (inclui '\\0').
     * @return false se chamada depois do begin() (fila já alocada).
     */
    bool configurarFila(uint16_t slots,
                        uint16_t topicoMax  = CONNECTIVITY_FILA_TOPICO_MAX,
                        uint16_t payloadMax = CONNECTIVITY_FILA_PAYLOAD_MAX);

    void         definirPoliticaFila(PoliticaFila politica);
    PoliticaFila obterPoliticaFila() const;

    /** @brief Buffer do PubSubClient (tamanho máximo de cada pacote MQTT). Padrão 256. */
    void configurarBufferMQTT(uint16_t tamanhoBytes);

    /**
     * @brief QoS e retained padrão usados por publicar().
     * @note O PubSubClient **publica sempre em QoS 0**; o QoS 1 só vale para inscrições.
     */
    void definirQoSPadrao(uint8_t qos, bool retained);

    /**
     * @brief Tópico de status com Last Will (LWT).
     * Ao conectar publica `online` (retained); se o ESP32 cair, o broker publica `offline`.
     * Sem esta chamada, mantém o comportamento antigo (mensagem no tópico 0).
     */
    void configurarStatus(const char* topico, const char* online = "online", const char* offline = "offline");

    // =======================================================================
    // Estado
    // =======================================================================

    bool     wifiConectado()   const;
    bool     mqttConectado()   const;
    uint16_t mensagensNaFila() const;
    String   ssidAtual()       const;   ///< SSID em uso (configurado ou salvo)

    // =======================================================================
    // Publicação
    // =======================================================================

    /** @return true se publicou na hora; false se foi para a fila ou descartada. */
    bool publicar(int indiceTopico, const char* mensagem, bool enfileirar = true);
    bool publicar(const char* topico, const char* mensagem, bool enfileirar = true);
    bool publicar(int indiceTopico, const String& mensagem, bool enfileirar = true)
        { return publicar(indiceTopico, mensagem.c_str(), enfileirar); }
    bool publicar(const char* topico, const String& mensagem, bool enfileirar = true)
        { return publicar(topico, mensagem.c_str(), enfileirar); }

    bool publicarQoS(const char* topico, const char* mensagem,
                     uint8_t qos, bool retained, bool enfileirar = true);

    const char* topicoPublicacao(int indice) const;
    const char* topicoRecebimento(int indice) const;

    // =======================================================================
    // Callbacks
    // =======================================================================

    void registrarCallbackMensagem         (CallbackMensagemMQTT cb);
    void registrarCallbackWiFiConectado    (CallbackWiFi cb);
    void registrarCallbackWiFiDesconectado (CallbackWiFi cb);
    void registrarCallbackMQTTConectado    (CallbackMQTT cb);
    void registrarCallbackMQTTDesconectado (CallbackMQTT cb);
    void registrarCallbackPortalSalvo      (CallbackPortalSalvo cb);

    // =======================================================================
    // Preferences (NVS) — namespace próprio da biblioteca
    // Seu código continua livre para usar Preferences com outros namespaces.
    // =======================================================================

    bool   salvarCredenciaisWiFi(const char* ssid, const char* senha);
    bool   apagarCredenciaisWiFi();
    bool   temCredenciaisWiFi() const;

    /** @brief Guarda um valor qualquer (chave com até 15 caracteres). */
    bool   salvarPreferencia(const char* chave, const String& valor);
    String lerPreferencia(const char* chave, const String& padrao = "") const;
    bool   apagarPreferencia(const char* chave);

    // =======================================================================
    // LittleFS
    // =======================================================================

    /** @brief Monta o LittleFS (a biblioteca chama sozinha quando precisa). */
    bool   iniciarLittleFS(bool formatarSeFalhar = false);
    /** @brief Lê um arquivo inteiro do LittleFS. String vazia se não existir. */
    String lerArquivo(const char* caminho);

    // =======================================================================
    // Portal de configuração WiFi (modo AP)
    // =======================================================================

    /**
     * @brief Abre um ponto de acesso com página para escolher a rede WiFi.
     *
     * Não bloqueia: o portal roda dentro do update(). Conecte o celular na
     * rede `nomeAP` e a página abre sozinha (captive portal) ou acesse
     * http://192.168.4.1
     *
     * A página vem de `/portal/index.html` no LittleFS (personalizável);
     * se o arquivo não existir, usa uma página embutida.
     *
     * @param nomeAP      Nome da rede do portal. nullptr → "ESP32-XXXX".
     * @param senhaAP     Senha (mín. 8 caracteres) ou nullptr para rede aberta.
     * @param timeoutSeg  Fecha o portal após N segundos sem uso (0 = nunca).
     */
    bool iniciarPortalConfiguracao(const char* nomeAP = nullptr,
                                   const char* senhaAP = nullptr,
                                   uint32_t timeoutSeg = 0);
    void pararPortalConfiguracao();
    bool portalAtivo() const;

    /** @brief Abre o portal sozinho após `falhas` tentativas de WiFi sem sucesso. */
    void definirPortalAutomatico(bool ativo,
                                 const char* nomeAP = nullptr,
                                 const char* senhaAP = nullptr,
                                 uint8_t falhas = CONNECTIVITY_FALHAS_PARA_PORTAL);

    /** @brief Pasta do LittleFS com os arquivos do portal (padrão "/portal"). */
    void definirPastaPortal(const char* pasta);

    // =======================================================================
    // English API — same functions, English names
    // =======================================================================

    void beginWiFiOnly(const WiFiConfig& w)                                         { beginSomenteWiFi(w); }
    void beginSimple(const WiFiConfig& w, const MQTTConfig& m, const TopicsConfig& t) { beginSimples(w, m, t); }
    // beginTLS(), beginAWS() and update() already have English names.
    void setHostname(const char* name)                                              { definirHostname(name); }

    bool        setQueueSize(uint16_t slots,
                             uint16_t topicMax   = CONNECTIVITY_FILA_TOPICO_MAX,
                             uint16_t payloadMax = CONNECTIVITY_FILA_PAYLOAD_MAX) { return configurarFila(slots, topicMax, payloadMax); }
    void        setQueuePolicy(QueuePolicy p)                                       { definirPoliticaFila(p); }
    QueuePolicy getQueuePolicy() const                                              { return obterPoliticaFila(); }
    void        setMQTTBufferSize(uint16_t bytes)                                   { configurarBufferMQTT(bytes); }
    void        setDefaultQoS(uint8_t qos, bool retained)                           { definirQoSPadrao(qos, retained); }
    void        setStatusTopic(const char* topic, const char* online = "online",
                               const char* offline = "offline")                     { configurarStatus(topic, online, offline); }

    bool     isWiFiConnected() const   { return wifiConectado(); }
    bool     isMQTTConnected() const   { return mqttConectado(); }
    uint16_t queuedMessages()  const   { return mensagensNaFila(); }
    String   currentSSID()     const   { return ssidAtual(); }

    bool publish(int topicIndex, const char* msg, bool queue = true)            { return publicar(topicIndex, msg, queue); }
    bool publish(const char* topic, const char* msg, bool queue = true)         { return publicar(topic, msg, queue); }
    bool publish(int topicIndex, const String& msg, bool queue = true)          { return publicar(topicIndex, msg.c_str(), queue); }
    bool publish(const char* topic, const String& msg, bool queue = true)       { return publicar(topic, msg.c_str(), queue); }
    bool publishQoS(const char* topic, const char* msg, uint8_t qos,
                    bool retained, bool queue = true)                           { return publicarQoS(topic, msg, qos, retained, queue); }
    const char* publishTopic(int i) const                                       { return topicoPublicacao(i); }
    const char* subscribeTopic(int i) const                                     { return topicoRecebimento(i); }

    void onMessage(MessageCallback cb)            { registrarCallbackMensagem(cb); }
    void onWiFiConnected(EventCallback cb)        { registrarCallbackWiFiConectado(cb); }
    void onWiFiDisconnected(EventCallback cb)     { registrarCallbackWiFiDesconectado(cb); }
    void onMQTTConnected(EventCallback cb)        { registrarCallbackMQTTConectado(cb); }
    void onMQTTDisconnected(EventCallback cb)     { registrarCallbackMQTTDesconectado(cb); }
    void onPortalSaved(PortalSavedCallback cb)    { registrarCallbackPortalSalvo(cb); }

    bool   saveWiFiCredentials(const char* ssid, const char* pass)              { return salvarCredenciaisWiFi(ssid, pass); }
    bool   clearWiFiCredentials()                                               { return apagarCredenciaisWiFi(); }
    bool   hasWiFiCredentials() const                                           { return temCredenciaisWiFi(); }
    bool   savePreference(const char* key, const String& value)                 { return salvarPreferencia(key, value); }
    String readPreference(const char* key, const String& def = "") const        { return lerPreferencia(key, def); }
    bool   removePreference(const char* key)                                    { return apagarPreferencia(key); }

    bool   beginLittleFS(bool formatOnFail = false)                             { return iniciarLittleFS(formatOnFail); }
    String readFile(const char* path)                                           { return lerArquivo(path); }

    bool startConfigPortal(const char* apName = nullptr, const char* apPass = nullptr,
                           uint32_t timeoutSec = 0)                             { return iniciarPortalConfiguracao(apName, apPass, timeoutSec); }
    void stopConfigPortal()                                                     { pararPortalConfiguracao(); }
    bool isConfigPortalActive() const                                           { return portalAtivo(); }
    void setAutoConfigPortal(bool enabled, const char* apName = nullptr,
                             const char* apPass = nullptr,
                             uint8_t failures = CONNECTIVITY_FALHAS_PARA_PORTAL) { definirPortalAutomatico(enabled, apName, apPass, failures); }
    void setPortalFolder(const char* folder)                                    { definirPastaPortal(folder); }

private:
    // ── Máquinas de estado ──────────────────────────────────────────────────
    enum class EstadoWiFi : uint8_t { DESCONECTADO, CONECTANDO, CONECTADO };
    enum class EstadoMQTT : uint8_t { DESCONECTADO, CONECTANDO, CONECTADO };

    EstadoWiFi _estadoWiFi = EstadoWiFi::DESCONECTADO;
    EstadoMQTT _estadoMQTT = EstadoMQTT::DESCONECTADO;

    static constexpr uint32_t WIFI_TIMEOUT_MS      = 15000;
    static constexpr uint32_t WIFI_RETRY_INTERVALO = 5000;
    static constexpr uint32_t MQTT_RETRY_INTERVALO = 5000;
    static constexpr uint32_t MQTT_PAUSA_LONGA     = 30000;
    static constexpr uint8_t  MQTT_MAX_TENTATIVAS  = 5;

    uint32_t _inicioConexaoWiFi   = 0;
    uint32_t _ultimaTentativaMQTT = 0;
    uint32_t _intervaloMQTT       = 0;
    uint8_t  _tentativasMQTT      = 0;
    uint8_t  _falhasWiFi          = 0;

    void _processarWiFi();
    void _processarMQTT();
    bool _tentarConectarMQTT();
    void _inscreverTopicos();
    void _marcarWiFiDesconectado();
    void _comecarConexaoWiFi();

    // ── Configuração (cópias próprias — não dependem do chamador) ──────────
    bool         _iniciado       = false;
    bool         _mqttHabilitado = false;
    ModoConexao  _modo           = ModoConexao::SIMPLES;
    ConfigTopicos _topicos       = {};
    PoliticaFila _politicaFila   = PoliticaFila::FILA_COMPLETA;

    String _ssid, _senhaWiFi, _hostname;
    String _broker, _clientId, _usuario, _senhaMQTT;
    int    _porta = 1883;

    // Certificados: ponteiro em uso + armazenamento quando veio do LittleFS
    const char* _ca  = nullptr;
    const char* _crt = nullptr;
    const char* _key = nullptr;
    String _pemCA, _pemCRT, _pemKey;

    String _topicoStatus, _statusOnline, _statusOffline;

    void _beginComum(const ConfigWiFi& wifi);
    void _carregarCredenciais(const ConfigWiFi& wifi);
    void _configurarClienteMQTT();
    const char* _resolverPEM(const char* valor, String& armazenamento, const char* nome);

    // ── Clientes de rede ────────────────────────────────────────────────────
    WiFiClient       _wifiCliente;
    WiFiClientSecure _wifiClienteSeguro;
    PubSubClient     _mqttClient;

    // ── Fila offline (FIFO circular) ────────────────────────────────────────
    struct MensagemFila {
        char*   topico;
        char*   payload;
        uint8_t qos;
        bool    retained;
    };

    MensagemFila* _fila        = nullptr;
    char*         _filaBuffer  = nullptr;
    uint16_t      _filaSlots   = CONNECTIVITY_FILA_SLOTS;
    uint16_t      _topicoMax   = CONNECTIVITY_FILA_TOPICO_MAX;
    uint16_t      _payloadMax  = CONNECTIVITY_FILA_PAYLOAD_MAX;
    uint16_t      _filaInicio  = 0;
    uint16_t      _filaFim     = 0;
    uint16_t      _filaTamanho = 0;

    uint8_t _qosPadrao      = 0;
    bool    _retainedPadrao = false;
    bool    _avisouQoS      = false;

    bool _alocarFila();
    void _liberarFila();
    bool _enfileirar(const char* topico, const char* payload, uint8_t qos, bool retained);
    void _drenaFila();
    bool _publicarDireto(const char* topico, const char* payload, uint8_t qos, bool retained);
    bool _online();

    // ── Callbacks ───────────────────────────────────────────────────────────
    CallbackMensagemMQTT _cbMensagem;
    CallbackWiFi         _cbWiFiConectado;
    CallbackWiFi         _cbWiFiDesconectado;
    CallbackMQTT         _cbMQTTConectado;
    CallbackMQTT         _cbMQTTDesconectado;
    CallbackPortalSalvo  _cbPortalSalvo;

    static ESP32Connectivity* _instancia;
    static void _callbackMQTTEstatico(char* topico, byte* payload, unsigned int tamanho);
    void        _callbackMQTT(char* topico, byte* payload, unsigned int tamanho);

    // ── LittleFS ────────────────────────────────────────────────────────────
    bool _littlefsMontado = false;

    // ── Portal ──────────────────────────────────────────────────────────────
    WebServer* _servidor        = nullptr;
    DNSServer* _dns             = nullptr;
    bool       _portalLigado    = false;
    bool       _portalAuto      = false;
    bool       _reabrirPortal   = false;   // portal salvou credenciais → reabre se falharem
    uint8_t    _falhasParaPortal = CONNECTIVITY_FALHAS_PARA_PORTAL;
    uint32_t   _portalUltimoUso = 0;
    uint32_t   _portalTimeoutMs = 0;
    uint32_t   _conectarEm      = 0;       // != 0 → fechar portal e conectar após salvar
    String     _portalNome, _portalSenha;
    String     _pastaPortal = CONNECTIVITY_PORTAL_PASTA;
    String     _cacheScan;

    void   _processarPortal();
    void   _rotaIndex();
    void   _rotaScan();
    void   _rotaSalvar();
    void   _rotaStatus();
    void   _rotaNaoEncontrada();
    bool   _servirArquivo(const String& caminho);
    String _nomeAPPadrao() const;
};

/** @brief Instância global (PT). */
extern ESP32Connectivity conectividade;
/** @brief Global instance (EN) — same object as `conectividade`. */
extern ESP32Connectivity& connectivity;

#endif // ESP32_CONNECTIVITY_H
