/**
 * @file ESP32Connectivity_Portal.cpp
 * @brief Portal de configuração WiFi (modo AP + captive portal).
 *
 * Endpoints (contrato para quem personalizar o HTML):
 *
 * | Método | Rota            | Resposta |
 * |--------|-----------------|----------|
 * | GET    | /               | `<pasta>/index.html` do LittleFS, ou a página embutida |
 * | GET    | /scan           | `{"scanning":bool,"networks":[{"ssid","rssi","secure","channel"}]}` |
 * | GET    | /scan?refresh=1 | Força nova varredura |
 * | POST   | /save           | Campos `ssid` e `password` (ou `senha`). Outros campos do formulário
 * |        |                 | são gravados no Preferences com o mesmo nome (até 15 caracteres) |
 * |        |                 | → `{"ok":true}` ou `{"ok":false,"erro":"..."}` |
 * | GET    | /status         | `{"ap","ssid","conectado","ip","versao"}` |
 * | GET    | /qualquer.ext   | Arquivo `<pasta>/qualquer.ext` do LittleFS (css, js, png, svg...) |
 *
 * Qualquer outra rota redireciona para "/" (é isso que faz o celular abrir a página sozinho).
 *
 * @author  professorThiago (https://github.com/professorThiago)
 * @license MIT
 */

#include "ESP32Connectivity.h"
#include "DebugManager.h"
#include "portal_padrao.h"
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>

static constexpr uint32_t ATRASO_CONEXAO_MS = 1500;   // tempo para a página receber a resposta

// =============================================================================
// Utilitários
// =============================================================================

static String escaparJSON(const String& s)
{
    String r;
    r.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++)
    {
        const char c = s[i];
        switch (c)
        {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n";  break;
            case '\r': r += "\\r";  break;
            case '\t': r += "\\t";  break;
            default:
                if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); r += b; }
                else r += c;
        }
    }
    return r;
}

static const char* tipoConteudo(const String& caminho)
{
    if (caminho.endsWith(".html") || caminho.endsWith(".htm")) return "text/html";
    if (caminho.endsWith(".css"))  return "text/css";
    if (caminho.endsWith(".js"))   return "application/javascript";
    if (caminho.endsWith(".json")) return "application/json";
    if (caminho.endsWith(".png"))  return "image/png";
    if (caminho.endsWith(".jpg") || caminho.endsWith(".jpeg")) return "image/jpeg";
    if (caminho.endsWith(".svg"))  return "image/svg+xml";
    if (caminho.endsWith(".ico"))  return "image/x-icon";
    if (caminho.endsWith(".woff2"))return "font/woff2";
    return "text/plain";
}

String ESP32Connectivity::_nomeAPPadrao() const
{
    const uint64_t mac = ESP.getEfuseMac();
    char nome[16];
    snprintf(nome, sizeof(nome), "ESP32-%02X%02X", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
    return String(nome);
}

// =============================================================================
// Configuração
// =============================================================================

void ESP32Connectivity::definirPortalAutomatico(bool ativo, const char* nomeAP,
                                                const char* senhaAP, uint8_t falhas)
{
    _portalAuto       = ativo;
    _portalNome       = nomeAP  ? nomeAP  : "";
    _portalSenha      = senhaAP ? senhaAP : "";
    _falhasParaPortal = falhas ? falhas : 1;
}

void ESP32Connectivity::definirPastaPortal(const char* pasta)
{
    _pastaPortal = (pasta && pasta[0]) ? pasta : CONNECTIVITY_PORTAL_PASTA;
    if (!_pastaPortal.startsWith("/")) _pastaPortal = "/" + _pastaPortal;
    if (_pastaPortal.length() > 1 && _pastaPortal.endsWith("/"))
        _pastaPortal.remove(_pastaPortal.length() - 1);
}

bool ESP32Connectivity::portalAtivo() const { return _portalLigado; }

// =============================================================================
// Abrir / fechar
// =============================================================================

bool ESP32Connectivity::iniciarPortalConfiguracao(const char* nomeAP, const char* senhaAP, uint32_t timeoutSeg)
{
    if (_portalLigado) return true;

    String nome  = (nomeAP && nomeAP[0]) ? String(nomeAP) : _nomeAPPadrao();
    String senha = senhaAP ? String(senhaAP) : String();
    if (senha.length() > 0 && senha.length() < 8)
    {
        debugAviso("Portal: senha do AP precisa de 8+ caracteres — abrindo rede SEM senha.");
        senha = "";
    }

    // Para a estação e avisa quem estava conectado
    if (_estadoWiFi != EstadoWiFi::DESCONECTADO) _marcarWiFiDesconectado();
    WiFi.disconnect();

    iniciarLittleFS();   // sem LittleFS o portal usa a página embutida

    WiFi.mode(WIFI_AP_STA);   // STA ativa para poder fazer a varredura
    if (!WiFi.softAP(nome.c_str(), senha.length() ? senha.c_str() : nullptr))
    {
        debugErro("Portal: falha ao criar o ponto de acesso.");
        WiFi.mode(WIFI_STA);
        return false;
    }
    delay(100);   // o IP do AP fica pronto logo após o softAP()
    const IPAddress ip = WiFi.softAPIP();

    _dns = new DNSServer();
    _dns->setErrorReplyCode(DNSReplyCode::NoError);
    _dns->start(53, "*", ip);   // todo domínio → ESP32 (captive portal)

    _servidor = new WebServer(80);
    _servidor->on("/",       HTTP_GET,  [this]() { _rotaIndex();  });
    _servidor->on("/scan",   HTTP_GET,  [this]() { _rotaScan();   });
    _servidor->on("/save",   HTTP_POST, [this]() { _rotaSalvar(); });
    _servidor->on("/status", HTTP_GET,  [this]() { _rotaStatus(); });
    _servidor->onNotFound(              [this]() { _rotaNaoEncontrada(); });
    _servidor->begin();

    _cacheScan = String();
    WiFi.scanNetworks(true);   // assíncrono — o resultado sai em /scan

    _portalLigado    = true;
    _portalUltimoUso = millis();
    _portalTimeoutMs = timeoutSeg * 1000UL;
    _conectarEm      = 0;

    debugInfo("Portal aberto! Conecte-se a rede '" + nome + "'" +
              (senha.length() ? " (com senha)" : " (aberta)") +
              " e acesse http://" + ip.toString());
    return true;
}

void ESP32Connectivity::pararPortalConfiguracao()
{
    if (!_portalLigado) return;

    if (_servidor) { _servidor->stop(); delete _servidor; _servidor = nullptr; }
    if (_dns)      { _dns->stop();      delete _dns;      _dns      = nullptr; }

    WiFi.scanDelete();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);

    _portalLigado = false;
    _conectarEm   = 0;
    _cacheScan    = String();
    _falhasWiFi   = 0;
    debugInfo("Portal fechado.");

    if (_ssid.length() > 0) _comecarConexaoWiFi();
}

// =============================================================================
// Processamento (chamado pelo update)
// =============================================================================

void ESP32Connectivity::_processarPortal()
{
    if (_dns)      _dns->processNextRequest();
    if (_servidor) _servidor->handleClient();

    // Credenciais salvas: dá tempo da página receber a resposta e conecta
    if (_conectarEm && millis() - _conectarEm >= ATRASO_CONEXAO_MS)
    {
        _reabrirPortal = true;    // se a senha estiver errada, o portal volta
        pararPortalConfiguracao();
        return;
    }

    // Timeout: só fecha se ninguém estiver conectado ao AP
    if (_portalTimeoutMs && WiFi.softAPgetStationNum() > 0) _portalUltimoUso = millis();
    if (_portalTimeoutMs && millis() - _portalUltimoUso >= _portalTimeoutMs)
    {
        debugAviso("Portal: tempo esgotado.");
        pararPortalConfiguracao();
    }
}

// =============================================================================
// Rotas
// =============================================================================

bool ESP32Connectivity::_servirArquivo(const String& caminho)
{
    if (!_littlefsMontado || caminho.indexOf("..") >= 0) return false;
    if (!LittleFS.exists(caminho)) return false;

    File f = LittleFS.open(caminho, "r");
    if (!f || f.isDirectory()) { if (f) f.close(); return false; }
    _servidor->streamFile(f, tipoConteudo(caminho));
    f.close();
    return true;
}

void ESP32Connectivity::_rotaIndex()
{
    _portalUltimoUso = millis();
    if (_servirArquivo(_pastaPortal + "/index.html")) return;
    _servidor->send(200, "text/html", String(PORTAL_HTML_PADRAO));
}

void ESP32Connectivity::_rotaScan()
{
    _portalUltimoUso = millis();
    const bool refresh = _servidor->hasArg("refresh");
    const int  n       = WiFi.scanComplete();

    if (n == WIFI_SCAN_RUNNING)
    {
        _servidor->send(200, "application/json", "{\"scanning\":true,\"networks\":" +
                        String(_cacheScan.length() ? _cacheScan : "[]") + "}");
        return;
    }

    if (n >= 0)
    {
        // Remove duplicadas (mesmo SSID em vários APs) mantendo o melhor sinal
        String json = "[";
        int  total = 0;
        bool usado[64] = { false };
        const int lim = n < 64 ? n : 64;
        for (int k = 0; k < lim; k++)
        {
            int melhor = -1;
            for (int i = 0; i < lim; i++)
                if (!usado[i] && (melhor < 0 || WiFi.RSSI(i) > WiFi.RSSI(melhor))) melhor = i;
            if (melhor < 0) break;
            usado[melhor] = true;

            const String ssid = WiFi.SSID(melhor);
            if (ssid.length() == 0) continue;          // rede oculta
            bool repetida = false;
            for (int i = 0; i < lim && !repetida; i++)
                if (i != melhor && usado[i] && WiFi.SSID(i) == ssid && WiFi.RSSI(i) >= WiFi.RSSI(melhor))
                    repetida = true;
            if (repetida) continue;

            if (total++) json += ",";
            json += "{\"ssid\":\"" + escaparJSON(ssid) + "\",\"rssi\":" + String(WiFi.RSSI(melhor)) +
                    ",\"secure\":" + String(WiFi.encryptionType(melhor) != WIFI_AUTH_OPEN ? "true" : "false") +
                    ",\"channel\":" + String(WiFi.channel(melhor)) + "}";
        }
        json += "]";
        _cacheScan = json;
        WiFi.scanDelete();
        debugVerbose("Portal: " + String(total) + " rede(s) encontrada(s).");
    }

    if (refresh || (n < 0 && _cacheScan.length() == 0))
    {
        WiFi.scanNetworks(true);
        _servidor->send(200, "application/json", "{\"scanning\":true,\"networks\":" +
                        String(_cacheScan.length() ? _cacheScan : "[]") + "}");
        return;
    }

    _servidor->send(200, "application/json", "{\"scanning\":false,\"networks\":" + _cacheScan + "}");
}

void ESP32Connectivity::_rotaSalvar()
{
    _portalUltimoUso = millis();

    const String ssid  = _servidor->arg("ssid");
    const String senha = _servidor->hasArg("password") ? _servidor->arg("password") : _servidor->arg("senha");

    if (ssid.length() == 0 || ssid.length() > 32)
    {
        _servidor->send(400, "application/json", "{\"ok\":false,\"erro\":\"Informe o nome da rede (ate 32 caracteres).\"}");
        return;
    }
    if (senha.length() > 0 && (senha.length() < 8 || senha.length() > 63))
    {
        _servidor->send(400, "application/json", "{\"ok\":false,\"erro\":\"A senha WiFi deve ter de 8 a 63 caracteres.\"}");
        return;
    }
    if (!salvarCredenciaisWiFi(ssid.c_str(), senha.c_str()))
    {
        _servidor->send(500, "application/json", "{\"ok\":false,\"erro\":\"Falha ao gravar na memoria.\"}");
        return;
    }

    // Campos extras do HTML personalizado → Preferences (mesmo nome do campo)
    for (int i = 0; i < _servidor->args(); i++)
    {
        const String nome = _servidor->argName(i);
        if (nome == "ssid" || nome == "password" || nome == "senha" || nome == "plain") continue;
        if (nome == "wifi_ssid" || nome == "wifi_pass") continue;   // reservadas
        if (nome.length() == 0 || nome.length() > 15)
        {
            debugAviso("Portal: campo '" + nome + "' ignorado (nome deve ter ate 15 caracteres).");
            continue;
        }
        salvarPreferencia(nome.c_str(), _servidor->arg(i));
        debugVerbose("Portal: campo extra '" + nome + "' salvo.");
    }

    _servidor->send(200, "application/json", "{\"ok\":true,\"ssid\":\"" + escaparJSON(ssid) + "\"}");

    _ssid      = ssid;
    _senhaWiFi = senha;
    _conectarEm = millis();
    if (_conectarEm == 0) _conectarEm = 1;

    if (_cbPortalSalvo) _cbPortalSalvo(ssid);
}

void ESP32Connectivity::_rotaStatus()
{
    _portalUltimoUso = millis();
    const bool conectado = (WiFi.status() == WL_CONNECTED);
    String json = "{\"ap\":\"" + escaparJSON(WiFi.softAPSSID()) +
                  "\",\"ssid\":\"" + escaparJSON(_ssid) +
                  "\",\"conectado\":" + (conectado ? "true" : "false") +
                  ",\"ip\":\"" + (conectado ? WiFi.localIP().toString() : String("")) +
                  "\",\"versao\":\"" CONNECTIVITY_VERSAO "\"}";
    _servidor->send(200, "application/json", json);
}

void ESP32Connectivity::_rotaNaoEncontrada()
{
    // Arquivos estáticos do portal (style.css, logo.png, ...)
    const String uri = _servidor->uri();
    if (uri.indexOf('.') > 0 && _servirArquivo(_pastaPortal + uri)) return;

    // Captive portal: qualquer outra coisa volta para a página principal
    _servidor->sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    _servidor->send(302, "text/plain", "");
}
