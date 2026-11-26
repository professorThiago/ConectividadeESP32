/**
 * @file ESP32Connectivity.cpp
 * @brief Núcleo: WiFi, MQTT e fila offline.
 *        (Portal em ESP32Connectivity_Portal.cpp; NVS/LittleFS em ESP32Connectivity_Storage.cpp)
 *
 * @author  professorThiago (https://github.com/professorThiago)
 * @version 4.0.0
 * @license MIT
 */

#include "ESP32Connectivity.h"
#include "DebugManager.h"

ESP32Connectivity  conectividade;
ESP32Connectivity& connectivity = conectividade;
ESP32Connectivity* ESP32Connectivity::_instancia = nullptr;

// =============================================================================
// Tradução de códigos de erro
// =============================================================================

static const char* traduzirStatusWiFi(int s)
{
    switch (s) {
        case 255: return "255: NO_SHIELD — hardware WiFi nao encontrado";
        case 0:   return "0: IDLE — WiFi ocioso";
        case 1:   return "1: NO_SSID_AVAIL — rede nao encontrada (verifique o SSID)";
        case 2:   return "2: SCAN_COMPLETED — varredura concluida";
        case 3:   return "3: CONNECTED";
        case 4:   return "4: CONNECT_FAILED — falha (verifique a senha)";
        case 5:   return "5: CONNECTION_LOST — sinal fraco ou AP reiniciou";
        case 6:   return "6: DISCONNECTED — desconectado";
        default:  return "DESCONHECIDO";
    }
}

static const char* traduzirCodigoMQTT(int c)
{
    switch (c) {
        case -4: return "-4: TIMEOUT — broker nao respondeu no keepalive";
        case -3: return "-3: CONNECTION_LOST — conexao TCP caiu";
        case -2: return "-2: CONNECT_FAILED — falha TCP/TLS (verifique host, porta e certificados)";
        case -1: return "-1: DISCONNECTED — desconectado normalmente";
        case  0: return "0: CONNECTED";
        case  1: return "1: BAD_PROTOCOL — versao do protocolo rejeitada";
        case  2: return "2: BAD_CLIENT_ID — client ID rejeitado";
        case  3: return "3: UNAVAILABLE — broker indisponivel";
        case  4: return "4: BAD_CREDENTIALS — usuario ou senha incorretos";
        case  5: return "5: UNAUTHORIZED — nao autorizado (ACL/certificado)";
        default: return "DESCONHECIDO";
    }
}

static inline const char* ouVazio(const char* s) { return s ? s : ""; }

ESP32Connectivity::~ESP32Connectivity()
{
    pararPortalConfiguracao();
    _liberarFila();
}

// =============================================================================
// Inicialização
// =============================================================================

void ESP32Connectivity::beginSomenteWiFi(const ConfigWiFi& wifi)
{
    _mqttHabilitado = false;
    _beginComum(wifi);
}

void ESP32Connectivity::beginSimples(const ConfigWiFi& wifi,
                                     const ConfigMQTT& mqtt,
                                     const ConfigTopicos& topicos)
{
    _modo     = ModoConexao::SIMPLES;
    _broker   = ouVazio(mqtt.broker);
    _porta    = mqtt.porta > 0 ? mqtt.porta : 1883;
    _clientId = ouVazio(mqtt.clientId);
    _usuario  = ouVazio(mqtt.usuario);
    _senhaMQTT= ouVazio(mqtt.senha);
    _topicos  = topicos;
    _mqttHabilitado = true;
    _beginComum(wifi);
}

void ESP32Connectivity::beginTLS(const ConfigWiFi& wifi,
                                 const ConfigMQTT& mqtt,
                                 const ConfigTLS& tls,
                                 const ConfigTopicos& topicos)
{
    _modo     = ModoConexao::TLS;
    _broker   = ouVazio(mqtt.broker);
    _porta    = mqtt.porta > 0 ? mqtt.porta : 8883;
    _clientId = ouVazio(mqtt.clientId);
    _usuario  = ouVazio(mqtt.usuario);
    _senhaMQTT= ouVazio(mqtt.senha);
    _ca       = _resolverPEM(tls.certificadoCA, _pemCA, "CA");
    _topicos  = topicos;
    _mqttHabilitado = true;
    _beginComum(wifi);
}

void ESP32Connectivity::beginAWS(const ConfigWiFi& wifi,
                                 const ConfigAWS& aws,
                                 const ConfigTopicos& topicos)
{
    _modo     = ModoConexao::AWS_IOT;
    _broker   = ouVazio(aws.endpoint);
    _porta    = aws.porta > 0 ? aws.porta : 8883;
    _clientId = ouVazio(aws.clientId);
    _usuario  = "";
    _senhaMQTT= "";
    _ca       = _resolverPEM(aws.certCA,      _pemCA,  "CA");
    _crt      = _resolverPEM(aws.certCRT,     _pemCRT, "certificado do dispositivo");
    _key      = _resolverPEM(aws.certPrivate, _pemKey, "chave privada");
    _topicos  = topicos;
    _mqttHabilitado = true;
    _beginComum(wifi);
}

void ESP32Connectivity::_beginComum(const ConfigWiFi& wifi)
{
    _instancia = this;

    debugInfo("==============================");
    debugInfo(" ESP32Connectivity v" CONNECTIVITY_VERSAO);
    debugInfo("==============================");

    if (_mqttHabilitado)
    {
        if (!_alocarFila()) return;
        _configurarClienteMQTT();
    }

    _carregarCredenciais(wifi);

    WiFi.persistent(false);          // evita gravar a senha na flash a cada begin()
    if (_hostname.length() > 0) WiFi.setHostname(_hostname.c_str());

    _iniciado          = true;
    _falhasWiFi        = 0;
    _intervaloMQTT     = MQTT_RETRY_INTERVALO;

    if (_portalLigado)
    {
        debugInfo("Portal aberto — o WiFi conecta depois que a rede for salva.");
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);    // a reconexão é feita pela máquina de estados

    if (_ssid.length() == 0)
    {
        debugAviso("Nenhuma rede WiFi configurada ou salva — abrindo o portal de configuracao.");
        iniciarPortalConfiguracao(_portalNome.length() ? _portalNome.c_str() : nullptr,
                                  _portalSenha.length() ? _portalSenha.c_str() : nullptr);
        return;
    }

    _comecarConexaoWiFi();
}

void ESP32Connectivity::_carregarCredenciais(const ConfigWiFi& wifi)
{
    if (wifi.ssid && wifi.ssid[0] != '\0')
    {
        _ssid      = wifi.ssid;
        _senhaWiFi = ouVazio(wifi.senha);
        debugVerbose("WiFi: usando credenciais do codigo.");
        return;
    }

    _ssid      = lerPreferencia("wifi_ssid", "");
    _senhaWiFi = lerPreferencia("wifi_pass", "");
    if (_ssid.length() > 0)
        debugInfo("WiFi: usando credenciais salvas (rede '" + _ssid + "').");
}

void ESP32Connectivity::_comecarConexaoWiFi()
{
    debugInfo("Conectando ao WiFi '" + _ssid + "'...");
    WiFi.disconnect();
    WiFi.begin(_ssid.c_str(), _senhaWiFi.c_str());
    _estadoWiFi        = EstadoWiFi::CONECTANDO;
    _inicioConexaoWiFi = millis();
}

void ESP32Connectivity::definirHostname(const char* nome)
{
    _hostname = ouVazio(nome);
}

// =============================================================================
// Configuração de fila e MQTT
// =============================================================================

bool ESP32Connectivity::configurarFila(uint16_t slots, uint16_t topicoMax, uint16_t payloadMax)
{
    if (_fila)
    {
        debugErro("configurarFila(): chame ANTES do begin().");
        return false;
    }
    _filaSlots  = slots      ? slots      : 1;
    _topicoMax  = topicoMax  > 8 ? topicoMax  : 8;
    _payloadMax = payloadMax > 8 ? payloadMax : 8;
    return true;
}

bool ESP32Connectivity::_alocarFila()
{
    if (_fila) return true;   // begin() chamado de novo: reaproveita a fila

    const size_t bytesTexto = (size_t)_filaSlots * (_topicoMax + _payloadMax);
    const size_t bytesMeta  = (size_t)_filaSlots * sizeof(MensagemFila);

#if CONNECTIVITY_USAR_PSRAM
    if (psramFound())
    {
        _filaBuffer = static_cast<char*>(ps_malloc(bytesTexto));
        if (_filaBuffer) debugInfo("Fila alocada na PSRAM.");
    }
    else
    {
        debugAviso("PSRAM nao encontrada — usando SRAM interna.");
    }
#endif
    if (!_filaBuffer) _filaBuffer = static_cast<char*>(malloc(bytesTexto));
    _fila = static_cast<MensagemFila*>(malloc(bytesMeta));

    if (!_filaBuffer || !_fila)
    {
        debugErro("FALHA CRITICA: sem memoria para a fila (" + String((unsigned)(bytesTexto + bytesMeta)) +
                  " bytes). Reduza com configurarFila().");
        _liberarFila();
        return false;
    }

    for (uint16_t i = 0; i < _filaSlots; i++)
    {
        _fila[i].topico   = _filaBuffer + (size_t)i * (_topicoMax + _payloadMax);
        _fila[i].payload  = _fila[i].topico + _topicoMax;
        _fila[i].topico[0] = _fila[i].payload[0] = '\0';
        _fila[i].qos      = 0;
        _fila[i].retained = false;
    }
    _filaInicio = _filaFim = _filaTamanho = 0;

    debugVerbose("Fila: " + String(_filaSlots) + " slots x " +
                 String(_topicoMax + _payloadMax) + " bytes = " +
                 String((unsigned)(bytesTexto + bytesMeta)) + " bytes");
    return true;
}

void ESP32Connectivity::_liberarFila()
{
    free(_filaBuffer);  _filaBuffer = nullptr;
    free(_fila);        _fila       = nullptr;
    _filaInicio = _filaFim = _filaTamanho = 0;
}

void ESP32Connectivity::configurarBufferMQTT(uint16_t tamanhoBytes)
{
    if (_mqttClient.setBufferSize(tamanhoBytes))
        debugVerbose("Buffer MQTT: " + String(tamanhoBytes) + " bytes");
    else
        debugErro("Buffer MQTT: sem memoria para " + String(tamanhoBytes) + " bytes");
}

void ESP32Connectivity::definirPoliticaFila(PoliticaFila politica)
{
    _politicaFila = politica;
    switch (politica)
    {
        case PoliticaFila::FILA_COMPLETA: debugVerbose("Politica de fila: FILA_COMPLETA"); break;
        case PoliticaFila::APENAS_ULTIMO: debugVerbose("Politica de fila: APENAS_ULTIMO"); break;
        case PoliticaFila::DESCARTAR:     debugVerbose("Politica de fila: DESCARTAR");     break;
    }
}

PoliticaFila ESP32Connectivity::obterPoliticaFila() const { return _politicaFila; }

void ESP32Connectivity::definirQoSPadrao(uint8_t qos, bool retained)
{
    _qosPadrao      = (qos > 1) ? 1 : qos;
    _retainedPadrao = retained;
}

void ESP32Connectivity::configurarStatus(const char* topico, const char* online, const char* offline)
{
    _topicoStatus  = ouVazio(topico);
    _statusOnline  = ouVazio(online);
    _statusOffline = ouVazio(offline);
}

// =============================================================================
// Loop principal
// =============================================================================

void ESP32Connectivity::update()
{
    if (_portalLigado) _processarPortal();

    _processarWiFi();
    _processarMQTT();

    if (_estadoMQTT == EstadoMQTT::CONECTADO)
    {
        _mqttClient.loop();
        _drenaFila();
    }
}

// =============================================================================
// Máquina de estados — WiFi
// =============================================================================

void ESP32Connectivity::_marcarWiFiDesconectado()
{
    bool estavaConectado = (_estadoWiFi == EstadoWiFi::CONECTADO);
    _estadoWiFi        = EstadoWiFi::DESCONECTADO;
    _inicioConexaoWiFi = millis();

    if (_estadoMQTT != EstadoMQTT::DESCONECTADO)
    {
        bool mqttEstava = (_estadoMQTT == EstadoMQTT::CONECTADO);
        _estadoMQTT = EstadoMQTT::DESCONECTADO;
        if (mqttEstava)
        {
            debugAviso("MQTT desconectado junto com o WiFi.");
            if (_cbMQTTDesconectado) _cbMQTTDesconectado();
        }
    }
    if (estavaConectado && _cbWiFiDesconectado) _cbWiFiDesconectado();
}

void ESP32Connectivity::_processarWiFi()
{
    // Com o portal aberto a estação fica parada (trocar de canal derruba o AP)
    if (_portalLigado || _ssid.length() == 0) return;

    const bool conectadoAgora = (WiFi.status() == WL_CONNECTED);

    switch (_estadoWiFi)
    {
        case EstadoWiFi::CONECTANDO:
        {
            if (conectadoAgora)
            {
                _estadoWiFi    = EstadoWiFi::CONECTADO;
                _falhasWiFi    = 0;
                _reabrirPortal = false;
                debugInfo("WiFi conectado! IP: " + WiFi.localIP().toString());
                debugVerbose("RSSI: " + String(WiFi.RSSI()) + " dBm");

                if (_cbWiFiConectado) _cbWiFiConectado();

                _estadoMQTT          = EstadoMQTT::DESCONECTADO;
                _tentativasMQTT      = 0;
                _intervaloMQTT       = 0;   // tenta o MQTT já
            }
            else if (millis() - _inicioConexaoWiFi > WIFI_TIMEOUT_MS)
            {
                const int ws = WiFi.status();
                _falhasWiFi++;
                debugAviso("WiFi: timeout (" + String(WIFI_TIMEOUT_MS / 1000) + "s). Falha " +
                           String(_falhasWiFi) + ".");
                debugErro("Status: " + String(traduzirStatusWiFi(ws)));
                if (ws == WL_NO_SSID_AVAIL) debugAviso("Dica: rede '" + _ssid + "' nao encontrada.");
                if (ws == WL_CONNECT_FAILED) debugAviso("Dica: verifique a senha do WiFi.");

                _estadoWiFi        = EstadoWiFi::DESCONECTADO;
                _inicioConexaoWiFi = millis();

                if ((_portalAuto || _reabrirPortal) && _falhasWiFi >= _falhasParaPortal)
                {
                    debugAviso("WiFi: " + String(_falhasWiFi) + " falhas seguidas — abrindo o portal.");
                    iniciarPortalConfiguracao(_portalNome.length() ? _portalNome.c_str() : nullptr,
                                              _portalSenha.length() ? _portalSenha.c_str() : nullptr);
                }
            }
            break;
        }

        case EstadoWiFi::CONECTADO:
        {
            if (!conectadoAgora)
            {
                const int ws = WiFi.status();
                debugErro("WiFi desconectado! Status: " + String(traduzirStatusWiFi(ws)));
                _marcarWiFiDesconectado();
            }
            break;
        }

        case EstadoWiFi::DESCONECTADO:
        {
            if (millis() - _inicioConexaoWiFi >= WIFI_RETRY_INTERVALO)
            {
                debugAviso("WiFi: tentando reconectar...");
                _comecarConexaoWiFi();
            }
            break;
        }
    }
}

// =============================================================================
// Máquina de estados — MQTT
// =============================================================================

void ESP32Connectivity::_processarMQTT()
{
    if (!_mqttHabilitado || _estadoWiFi != EstadoWiFi::CONECTADO) return;

    switch (_estadoMQTT)
    {
        case EstadoMQTT::DESCONECTADO:
        {
            if (millis() - _ultimaTentativaMQTT < _intervaloMQTT) return;

            if (_tentativasMQTT >= MQTT_MAX_TENTATIVAS)
            {
                debugAviso("MQTT: " + String(MQTT_MAX_TENTATIVAS) + " falhas. Pausando " +
                           String(MQTT_PAUSA_LONGA / 1000) + "s...");
                _tentativasMQTT      = 0;
                _ultimaTentativaMQTT = millis();
                _intervaloMQTT       = MQTT_PAUSA_LONGA;
                return;
            }

            _tentativasMQTT++;
            _ultimaTentativaMQTT = millis();
            _intervaloMQTT       = MQTT_RETRY_INTERVALO;
            debugVerbose("MQTT: tentativa " + String(_tentativasMQTT) + "/" + String(MQTT_MAX_TENTATIVAS));
            _estadoMQTT = EstadoMQTT::CONECTANDO;
            break;
        }

        case EstadoMQTT::CONECTANDO:
        {
            if (_tentarConectarMQTT())
            {
                _estadoMQTT     = EstadoMQTT::CONECTADO;
                _tentativasMQTT = 0;
                debugInfo("MQTT conectado! (client ID: " + _clientId + ")");

                _inscreverTopicos();

                if (_topicoStatus.length() > 0)
                    _publicarDireto(_topicoStatus.c_str(), _statusOnline.c_str(), 0, true);
                else if (_topicos.nPublicar > 0)   // comportamento da v3
                    _publicarDireto(_topicos.publicar[0], "ESP32 conectado ao MQTT", 0, false);

                if (_cbMQTTConectado) _cbMQTTConectado();
            }
            else
            {
                const int codigo = _mqttClient.state();
                debugErro("MQTT: falha na conexao! Codigo: " + String(traduzirCodigoMQTT(codigo)));
                if (codigo == 4)  debugAviso("Dica: verifique usuario e senha do broker.");
                if (codigo == 2)  debugAviso("Dica: tente outro clientId.");
                if (codigo == -2) debugAviso("Dica: verifique broker, porta e certificados.");
                if (codigo == 5)  debugAviso("Dica: verifique certificados ou permissoes ACL.");
                _estadoMQTT = EstadoMQTT::DESCONECTADO;
            }
            break;
        }

        case EstadoMQTT::CONECTADO:
        {
            if (!_mqttClient.connected())
            {
                debugErro("MQTT desconectado! Codigo: " + String(traduzirCodigoMQTT(_mqttClient.state())));
                _estadoMQTT          = EstadoMQTT::DESCONECTADO;
                _ultimaTentativaMQTT = millis();
                _intervaloMQTT       = MQTT_RETRY_INTERVALO;
                if (_cbMQTTDesconectado) _cbMQTTDesconectado();
            }
            break;
        }
    }
}

bool ESP32Connectivity::_tentarConectarMQTT()
{
    // Observação: connect() do PubSubClient bloqueia durante o handshake
    // (TLS pode levar 1–3 s). O timeout é limitado em _configurarClienteMQTT().
    const char* usuario = _usuario.length()   ? _usuario.c_str()   : nullptr;
    const char* senha   = _senhaMQTT.length() ? _senhaMQTT.c_str() : nullptr;
    const char* lwt     = _topicoStatus.length() ? _topicoStatus.c_str() : nullptr;

    debugVerbose(String("MQTT: conectando") + (usuario ? " com usuario/senha" : " anonimamente") +
                 (lwt ? " (com LWT)" : "") + "...");

    return _mqttClient.connect(_clientId.c_str(), usuario, senha,
                               lwt, 0, true, lwt ? _statusOffline.c_str() : nullptr);
}

void ESP32Connectivity::_inscreverTopicos()
{
    for (int i = 0; i < _topicos.nReceber; i++)
    {
        const char* t = _topicos.receber[i];
        if (!t || t[0] == '\0') continue;
        if (_mqttClient.subscribe(t, 1))
            debugInfo("MQTT: inscrito em '" + String(t) + "'");
        else
            debugErro("MQTT: falha ao inscrever em '" + String(t) + "'");
    }
}

void ESP32Connectivity::_configurarClienteMQTT()
{
    debugInfo(String("Modo MQTT: ") +
              (_modo == ModoConexao::SIMPLES ? "SIMPLES" :
               _modo == ModoConexao::TLS     ? "TLS"     : "AWS IOT CORE") +
              " -> " + _broker + ":" + String(_porta));

    if (_clientId.length() == 0)
    {
        _clientId = _nomeAPPadrao();
        debugAviso("clientId vazio — usando '" + _clientId + "'.");
    }

    if (_modo == ModoConexao::SIMPLES)
    {
        _mqttClient.setClient(_wifiCliente);
    }
    else
    {
        if (_ca) _wifiClienteSeguro.setCACert(_ca);
        else if (_modo == ModoConexao::TLS)
        {
            debugAviso("Certificado CA ausente — usando setInsecure() (APENAS PARA TESTES!)");
            _wifiClienteSeguro.setInsecure();
        }

        if (_modo == ModoConexao::AWS_IOT)
        {
            if (!_ca || !_crt || !_key)
                debugErro("AWS IoT: faltam certificados (CA, dispositivo e chave privada sao obrigatorios).");
            if (_crt) _wifiClienteSeguro.setCertificate(_crt);
            if (_key) _wifiClienteSeguro.setPrivateKey(_key);
        }
        _wifiClienteSeguro.setHandshakeTimeout(8);
        _mqttClient.setClient(_wifiClienteSeguro);
    }

    _mqttClient.setServer(_broker.c_str(), (uint16_t)_porta);
    _mqttClient.setSocketTimeout(5);
    _mqttClient.setCallback(_callbackMQTTEstatico);
}

// =============================================================================
// Publicação
// =============================================================================

bool ESP32Connectivity::_online()
{
    return _estadoMQTT == EstadoMQTT::CONECTADO &&
           WiFi.status() == WL_CONNECTED &&
           _mqttClient.connected();
}

bool ESP32Connectivity::publicar(int indiceTopico, const char* mensagem, bool enfileirar)
{
    return publicarQoS(topicoPublicacao(indiceTopico), mensagem, _qosPadrao, _retainedPadrao, enfileirar);
}

bool ESP32Connectivity::publicar(const char* topico, const char* mensagem, bool enfileirar)
{
    return publicarQoS(topico, mensagem, _qosPadrao, _retainedPadrao, enfileirar);
}

bool ESP32Connectivity::publicarQoS(const char* topico, const char* mensagem,
                                    uint8_t qos, bool retained, bool enfileirar)
{
    if (!_mqttHabilitado || !_fila)
    {
        debugErro("publicar(): MQTT nao iniciado (use beginSimples/beginTLS/beginAWS).");
        return false;
    }
    if (!topico || topico[0] == '\0')
    {
        debugErro("publicar(): topico vazio ou invalido.");
        return false;
    }
    if (!mensagem) mensagem = "";

    if (qos > 0 && !_avisouQoS)
    {
        debugAviso("PubSubClient publica sempre em QoS 0 (QoS 1 vale so para inscricoes).");
        _avisouQoS = true;
    }

    // Mensagem que nunca caberia no buffer do PubSubClient travaria a fila
    const size_t pacote = strlen(topico) + strlen(mensagem) + 7;
    if (pacote > _mqttClient.getBufferSize())
    {
        debugErro("publicar(): mensagem de " + String((unsigned)pacote) + " bytes excede o buffer MQTT (" +
                  String(_mqttClient.getBufferSize()) + "). Use configurarBufferMQTT().");
        return false;
    }

    const bool online = _online();

    // Sem fila: envia agora ou descarta
    if (!enfileirar || _politicaFila == PoliticaFila::DESCARTAR)
    {
        if (online) return _publicarDireto(topico, mensagem, qos, retained);
        debugVerbose("Offline: mensagem descartada.");
        return false;
    }

    // Online e sem pendências: envia direto (sem truncar)
    if (online && _filaTamanho == 0 && _publicarDireto(topico, mensagem, qos, retained))
        return true;

    // Offline, falha no envio, ou há mensagens anteriores (preserva a ordem)
    if (!_enfileirar(topico, mensagem, qos, retained)) return false;
    if (online) _drenaFila();
    return false;
}

bool ESP32Connectivity::_publicarDireto(const char* topico, const char* payload,
                                        uint8_t qos, bool retained)
{
    (void)qos;
    const bool ok = _mqttClient.publish(topico, payload, retained);
    if (ok)
    {
        debugInfo("MQTT publicado em '" + String(topico) + "'" + (retained ? " [retained]" : ""));
        debugTudo("Payload: " + String(payload));
    }
    else
    {
        debugErro("MQTT: falha ao publicar em '" + String(topico) + "' — " +
                  String(traduzirCodigoMQTT(_mqttClient.state())));
    }
    return ok;
}

// =============================================================================
// Fila offline
// =============================================================================

bool ESP32Connectivity::_enfileirar(const char* topico, const char* payload,
                                    uint8_t qos, bool retained)
{
    const size_t lt = strlen(topico), lp = strlen(payload);
    if (lt >= _topicoMax)
    {
        debugErro("Fila: topico com " + String((unsigned)lt) + " bytes (max " + String(_topicoMax - 1) +
                  "). Aumente com configurarFila().");
        return false;
    }
    if (lp >= _payloadMax)
    {
        debugErro("Fila: payload com " + String((unsigned)lp) + " bytes (max " + String(_payloadMax - 1) +
                  "). Aumente com configurarFila().");
        return false;
    }

    // APENAS_ULTIMO: substitui a mensagem pendente do MESMO tópico
    if (_politicaFila == PoliticaFila::APENAS_ULTIMO)
    {
        for (uint16_t n = 0; n < _filaTamanho; n++)
        {
            MensagemFila& m = _fila[(_filaInicio + n) % _filaSlots];
            if (strcmp(m.topico, topico) == 0)
            {
                memcpy(m.payload, payload, lp + 1);
                m.qos = qos;
                m.retained = retained;
                debugVerbose("Fila: mensagem de '" + String(topico) + "' substituida.");
                return true;
            }
        }
    }

    if (_filaTamanho >= _filaSlots)
    {
        debugAviso("Fila cheia (" + String(_filaSlots) + ") — descartando a mais antiga.");
        _filaInicio = (_filaInicio + 1) % _filaSlots;
        _filaTamanho--;
    }

    MensagemFila& slot = _fila[_filaFim];
    memcpy(slot.topico,  topico,  lt + 1);
    memcpy(slot.payload, payload, lp + 1);
    slot.qos      = qos;
    slot.retained = retained;

    _filaFim = (_filaFim + 1) % _filaSlots;
    _filaTamanho++;
    debugVerbose("Mensagem enfileirada. Fila: " + String(_filaTamanho) + "/" + String(_filaSlots));
    return true;
}

void ESP32Connectivity::_drenaFila()
{
    if (_filaTamanho == 0) return;

    uint16_t enviadas = 0;
    while (_filaTamanho > 0 && enviadas < CONNECTIVITY_DRENAR_POR_CICLO && _mqttClient.connected())
    {
        MensagemFila& slot = _fila[_filaInicio];
        if (!_publicarDireto(slot.topico, slot.payload, slot.qos, slot.retained)) break;
        _filaInicio = (_filaInicio + 1) % _filaSlots;
        _filaTamanho--;
        enviadas++;
    }

    if (enviadas > 0 && _filaTamanho == 0) debugInfo("Fila drenada.");
}

// =============================================================================
// Estado e tópicos
// =============================================================================

bool     ESP32Connectivity::wifiConectado()   const { return _estadoWiFi == EstadoWiFi::CONECTADO; }
bool     ESP32Connectivity::mqttConectado()   const { return _estadoMQTT == EstadoMQTT::CONECTADO; }
uint16_t ESP32Connectivity::mensagensNaFila() const { return _filaTamanho; }
String   ESP32Connectivity::ssidAtual()       const { return _ssid; }

const char* ESP32Connectivity::topicoPublicacao(int i) const
{
    if (i < 0 || i >= _topicos.nPublicar || !_topicos.publicar) {
        debugErro("topicoPublicacao(): indice invalido: " + String(i));
        return "";
    }
    return _topicos.publicar[i];
}

const char* ESP32Connectivity::topicoRecebimento(int i) const
{
    if (i < 0 || i >= _topicos.nReceber || !_topicos.receber) {
        debugErro("topicoRecebimento(): indice invalido: " + String(i));
        return "";
    }
    return _topicos.receber[i];
}

// =============================================================================
// Callbacks
// =============================================================================

void ESP32Connectivity::registrarCallbackMensagem        (CallbackMensagemMQTT cb) { _cbMensagem         = cb; }
void ESP32Connectivity::registrarCallbackWiFiConectado   (CallbackWiFi cb)         { _cbWiFiConectado    = cb; }
void ESP32Connectivity::registrarCallbackWiFiDesconectado(CallbackWiFi cb)         { _cbWiFiDesconectado = cb; }
void ESP32Connectivity::registrarCallbackMQTTConectado   (CallbackMQTT cb)         { _cbMQTTConectado    = cb; }
void ESP32Connectivity::registrarCallbackMQTTDesconectado(CallbackMQTT cb)         { _cbMQTTDesconectado = cb; }
void ESP32Connectivity::registrarCallbackPortalSalvo     (CallbackPortalSalvo cb)  { _cbPortalSalvo      = cb; }

void ESP32Connectivity::_callbackMQTTEstatico(char* topico, byte* payload, unsigned int tamanho)
{
    if (_instancia) _instancia->_callbackMQTT(topico, payload, tamanho);
}

void ESP32Connectivity::_callbackMQTT(char* topico, byte* payload, unsigned int tamanho)
{
    String mensagem;
    mensagem.reserve(tamanho);
    for (unsigned int i = 0; i < tamanho; i++) mensagem += (char)payload[i];

    debugInfo("Mensagem recebida em '" + String(topico) + "'");
    debugTudo("Payload (" + String(tamanho) + " bytes): " + mensagem);

    if (_cbMensagem) _cbMensagem(topico, mensagem);
    else             debugAviso("Mensagem recebida mas nenhum callback registrado.");
}
