/**
 * @file ESP32Connectivity_Storage.cpp
 * @brief Preferences (NVS) e LittleFS.
 *
 * - Preferences: a biblioteca abre o namespace CONNECTIVITY_NVS_NAMESPACE só
 *   durante cada leitura/escrita e fecha em seguida. Seu código pode usar
 *   Preferences com outros namespaces normalmente, ao mesmo tempo.
 * - LittleFS: certificados, chaves e os arquivos do portal.
 *
 * @author  professorThiago (https://github.com/professorThiago)
 * @license MIT
 */

#include "ESP32Connectivity.h"
#include "DebugManager.h"
#include <Preferences.h>
#include <LittleFS.h>

static const char* CHAVE_SSID  = "wifi_ssid";
static const char* CHAVE_SENHA = "wifi_pass";

static bool chaveValida(const char* chave)
{
    if (!chave || chave[0] == '\0' || strlen(chave) > 15)
    {
        debugErro("Preferences: chave invalida (1 a 15 caracteres): '" + String(chave ? chave : "") + "'");
        return false;
    }
    return true;
}

// =============================================================================
// Preferences (NVS)
// =============================================================================

bool ESP32Connectivity::salvarPreferencia(const char* chave, const String& valor)
{
    if (!chaveValida(chave)) return false;
    Preferences p;
    if (!p.begin(CONNECTIVITY_NVS_NAMESPACE, false))
    {
        debugErro("Preferences: nao foi possivel abrir '" CONNECTIVITY_NVS_NAMESPACE "'.");
        return false;
    }
    const bool ok = p.putString(chave, valor) == valor.length();
    p.end();
    if (!ok) debugErro("Preferences: falha ao gravar '" + String(chave) + "'.");
    return ok;
}

String ESP32Connectivity::lerPreferencia(const char* chave, const String& padrao) const
{
    if (!chaveValida(chave)) return padrao;
    Preferences p;
    // readOnly=false cria o namespace se ainda não existir (evita erro no log)
    if (!p.begin(CONNECTIVITY_NVS_NAMESPACE, false)) return padrao;
    String valor = p.isKey(chave) ? p.getString(chave, padrao) : padrao;
    p.end();
    return valor;
}

bool ESP32Connectivity::apagarPreferencia(const char* chave)
{
    if (!chaveValida(chave)) return false;
    Preferences p;
    if (!p.begin(CONNECTIVITY_NVS_NAMESPACE, false)) return false;
    const bool ok = !p.isKey(chave) || p.remove(chave);
    p.end();
    return ok;
}

bool ESP32Connectivity::salvarCredenciaisWiFi(const char* ssid, const char* senha)
{
    if (!ssid || ssid[0] == '\0' || strlen(ssid) > 32)
    {
        debugErro("salvarCredenciaisWiFi(): SSID invalido (1 a 32 caracteres).");
        return false;
    }
    const String s = senha ? senha : "";
    if (s.length() > 0 && (s.length() < 8 || s.length() > 63))
    {
        debugErro("salvarCredenciaisWiFi(): senha WPA precisa ter de 8 a 63 caracteres.");
        return false;
    }

    const bool ok = salvarPreferencia(CHAVE_SSID, ssid) && salvarPreferencia(CHAVE_SENHA, s);
    if (ok) debugInfo("Credenciais WiFi salvas (rede '" + String(ssid) + "').");
    return ok;
}

bool ESP32Connectivity::apagarCredenciaisWiFi()
{
    const bool ok = apagarPreferencia(CHAVE_SSID) && apagarPreferencia(CHAVE_SENHA);
    if (ok) debugInfo("Credenciais WiFi apagadas.");
    return ok;
}

bool ESP32Connectivity::temCredenciaisWiFi() const
{
    return lerPreferencia(CHAVE_SSID, "").length() > 0;
}

// =============================================================================
// LittleFS
// =============================================================================

bool ESP32Connectivity::iniciarLittleFS(bool formatarSeFalhar)
{
    if (_littlefsMontado) return true;

    // Se o seu código já montou o LittleFS, begin() apenas confirma.
    _littlefsMontado = LittleFS.begin(formatarSeFalhar);
    if (_littlefsMontado)
        debugVerbose("LittleFS montado.");
    else
        debugErro("LittleFS: falha ao montar. Envie a pasta data/ com "
                  "'pio run -t uploadfs' (PlatformIO) ou o plugin LittleFS Upload (Arduino IDE) "
                  "e confira 'board_build.filesystem = littlefs'.");
    return _littlefsMontado;
}

String ESP32Connectivity::lerArquivo(const char* caminho)
{
    if (!caminho || caminho[0] != '/') return String();
    if (!iniciarLittleFS()) return String();

    if (!LittleFS.exists(caminho))
    {
        debugErro("LittleFS: arquivo '" + String(caminho) + "' nao encontrado.");
        return String();
    }

    File f = LittleFS.open(caminho, "r");
    if (!f)
    {
        debugErro("LittleFS: nao foi possivel abrir '" + String(caminho) + "'.");
        return String();
    }

    String conteudo;
    conteudo.reserve(f.size() + 1);
    while (f.available()) conteudo += (char)f.read();
    f.close();
    return conteudo;
}

/**
 * Resolve um certificado: caminho no LittleFS ("/certs/ca.pem") ou conteúdo PEM.
 * O WiFiClientSecure guarda só o ponteiro, então o conteúdo lido do arquivo
 * fica em `armazenamento` (membro da classe) enquanto o objeto existir.
 */
const char* ESP32Connectivity::_resolverPEM(const char* valor, String& armazenamento, const char* nome)
{
    armazenamento = String();
    if (!valor || valor[0] == '\0') return nullptr;

    if (valor[0] == '/')
    {
        armazenamento = lerArquivo(valor);
        if (armazenamento.length() == 0)
        {
            debugErro(String("Certificado ") + nome + ": nao foi possivel ler '" + valor + "'.");
            return nullptr;
        }
        if (armazenamento.indexOf("-----BEGIN") < 0)
            debugAviso(String("Certificado ") + nome + ": '" + valor + "' nao parece um PEM.");
        debugInfo(String("Certificado ") + nome + " carregado de '" + valor + "' (" +
                  String(armazenamento.length()) + " bytes).");
        return armazenamento.c_str();
    }

    if (strstr(valor, "-----BEGIN") == nullptr)
    {
        debugAviso(String("Certificado ") + nome + ": conteudo nao parece PEM "
                   "(caminhos do LittleFS devem comecar com '/').");
        // Bloco "-----BEGIN/END" vazio do template também cai aqui
    }
    else if (strstr(valor, "-----BEGIN") && strlen(valor) < 100)
    {
        debugAviso(String("Certificado ") + nome + ": PEM vazio — preencha o certificado.");
        return nullptr;
    }
    return valor;
}
