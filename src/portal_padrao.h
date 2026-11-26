/**
 * @file portal_padrao.h
 * @brief Página embutida do portal — usada quando /portal/index.html não existe no LittleFS.
 *
 * Para personalizar, NÃO edite aqui: copie examples/04_PortalWiFi/data/portal/index.html
 * para a pasta data/portal/ do seu projeto e envie com "pio run -t uploadfs".
 *
 * Gerado a partir de examples/04_PortalWiFi/data/portal/index.html.
 */
#pragma once
#include <Arduino.h>

static const char PORTAL_HTML_PADRAO[] PROGMEM = R"PORTAL(<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Configurar WiFi</title>
<style>
  :root {
    --cor: #0b5ed7;
    --fundo: #f3f5f8;
    --cartao: #ffffff;
    --texto: #1d2430;
    --suave: #6b7480;
    --borda: #dfe3e8;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
    background: var(--fundo); color: var(--texto);
  }
  main { max-width: 420px; margin: 0 auto; padding: 24px 16px 40px; }
  header { margin-bottom: 20px; }
  h1 { font-size: 1.35rem; margin: 0 0 4px; }
  .sub { color: var(--suave); font-size: .9rem; margin: 0; }
  .cartao {
    background: var(--cartao); border: 1px solid var(--borda);
    border-radius: 12px; padding: 16px; margin-bottom: 16px;
  }
  .topo { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; }
  .topo h2 { font-size: 1rem; margin: 0; }
  button.link { background: none; border: 0; color: var(--cor); font-size: .9rem; cursor: pointer; padding: 4px; }
  ul { list-style: none; margin: 0; padding: 0; }
  li {
    display: flex; align-items: center; gap: 10px; padding: 12px 8px;
    border-radius: 8px; cursor: pointer;
  }
  li:hover, li.sel { background: #eaf1fd; }
  li .nome { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  li .info { color: var(--suave); font-size: .8rem; }
  .barras { display: inline-flex; align-items: flex-end; gap: 2px; height: 14px; }
  .barras i { width: 4px; background: var(--borda); border-radius: 1px; }
  .barras i.on { background: var(--cor); }
  label { display: block; font-size: .85rem; color: var(--suave); margin: 12px 0 4px; }
  input {
    width: 100%; padding: 12px; font-size: 1rem; border: 1px solid var(--borda);
    border-radius: 8px; background: #fff; color: var(--texto);
  }
  input:focus { outline: 2px solid var(--cor); border-color: transparent; }
  .senha { position: relative; }
  .senha button { position: absolute; right: 6px; top: 6px; bottom: 6px; }
  button.principal {
    width: 100%; margin-top: 16px; padding: 13px; font-size: 1rem; font-weight: 600;
    background: var(--cor); color: #fff; border: 0; border-radius: 8px; cursor: pointer;
  }
  button.principal:disabled { opacity: .6; }
  .msg { padding: 12px; border-radius: 8px; font-size: .9rem; margin-top: 12px; display: none; }
  .msg.ok { display: block; background: #e7f6ec; color: #136c2e; }
  .msg.erro { display: block; background: #fdecec; color: #a12121; }
  .vazio { color: var(--suave); font-size: .9rem; padding: 12px 8px; }
  footer { text-align: center; color: var(--suave); font-size: .75rem; }
</style>
</head>
<body>
<main>
  <header>
    <h1>Configurar WiFi</h1>
    <p class="sub">Escolha a rede que o dispositivo deve usar.</p>
  </header>

  <section class="cartao">
    <div class="topo">
      <h2>Redes encontradas</h2>
      <button class="link" id="atualizar" type="button">Atualizar</button>
    </div>
    <ul id="lista"><li class="vazio">Procurando redes…</li></ul>
  </section>

  <form class="cartao" id="form">
    <label for="ssid">Nome da rede (SSID)</label>
    <input id="ssid" name="ssid" maxlength="32" autocomplete="off" required>

    <label for="password">Senha</label>
    <div class="senha">
      <input id="password" name="password" type="password" maxlength="63" autocomplete="off">
      <button class="link" type="button" id="ver">mostrar</button>
    </div>

    
    <button class="principal" id="salvar" type="submit">Salvar e conectar</button>
    <div class="msg" id="msg"></div>
  </form>

  <footer id="rodape"></footer>
</main>

<script>
const $ = (id) => document.getElementById(id);

function barras(rssi) {
  const n = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1;
  let h = '<span class="barras">';
  for (let i = 1; i <= 4; i++) h += `<i class="${i <= n ? 'on' : ''}" style="height:${i * 25}%"></i>`;
  return h + '</span>';
}

function escapar(t) {
  const d = document.createElement('div');
  d.textContent = t;
  return d.innerHTML;
}

function mostrar(redes) {
  const lista = $('lista');
  if (!redes.length) {
    lista.innerHTML = '<li class="vazio">Nenhuma rede encontrada.</li>';
    return;
  }
  lista.innerHTML = '';
  redes.forEach((r) => {
    const li = document.createElement('li');
    li.innerHTML = `${barras(r.rssi)}<span class="nome">${escapar(r.ssid)}</span>` +
                   `<span class="info">${r.secure ? '🔒' : 'aberta'}</span>`;
    li.onclick = () => {
      document.querySelectorAll('li.sel').forEach((e) => e.classList.remove('sel'));
      li.classList.add('sel');
      $('ssid').value = r.ssid;
      $('password').value = '';
      if (r.secure) $('password').focus();
    };
    lista.appendChild(li);
  });
}

async function procurar(forcar) {
  if (forcar) $('lista').innerHTML = '<li class="vazio">Procurando redes…</li>';
  try {
    const r = await fetch('/scan' + (forcar ? '?refresh=1' : ''));
    const j = await r.json();
    if (j.networks.length || !j.scanning) mostrar(j.networks);
    if (j.scanning) setTimeout(() => procurar(false), 1500);
  } catch (e) {
    setTimeout(() => procurar(false), 3000);
  }
}

$('atualizar').onclick = () => procurar(true);

$('ver').onclick = () => {
  const p = $('password');
  p.type = p.type === 'password' ? 'text' : 'password';
  $('ver').textContent = p.type === 'password' ? 'mostrar' : 'ocultar';
};

$('form').onsubmit = async (ev) => {
  ev.preventDefault();
  const msg = $('msg');
  msg.className = 'msg';
  $('salvar').disabled = true;
  try {
    const r = await fetch('/save', { method: 'POST', body: new URLSearchParams(new FormData($('form'))) });
    const j = await r.json();
    if (j.ok) {
      msg.className = 'msg ok';
      msg.textContent = `Rede "${j.ssid}" salva! O dispositivo vai se conectar agora. ` +
                        'Se a senha estiver errada, esta rede de configuração volta a aparecer.';
      return;
    }
    msg.className = 'msg erro';
    msg.textContent = j.erro || 'Não foi possível salvar.';
  } catch (e) {
    msg.className = 'msg erro';
    msg.textContent = 'Sem resposta do dispositivo. Tente novamente.';
  }
  $('salvar').disabled = false;
};

fetch('/status').then((r) => r.json()).then((s) => {
  $('rodape').textContent = `${s.ap} · ConectividadeESP32 v${s.versao}`;
  if (s.ssid) $('ssid').value = s.ssid;
}).catch(() => {});

procurar(false);
</script>
</body>
</html>
)PORTAL";
