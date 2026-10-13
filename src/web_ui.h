#ifndef WEB_UI_H_
#define WEB_UI_H_

#include <Arduino.h>

const char kControlPage[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#153744">
  <title>Blue Star AC</title>
  <style>
    :root { color-scheme: light; --ink:#193944; --muted:#677b83;
      --accent:#087d8c; --line:#dce5e8; --paper:#fdfefe; }
    * { box-sizing:border-box; }
    body { margin:0; background:#edf2f4; color:var(--ink);
      font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif; }
    main { max-width:450px; margin:28px auto; padding:0 18px; }
    header { display:flex; align-items:center; justify-content:space-between;
      gap:10px; margin:0 4px 18px; }
    h1 { font-size:22px; margin:0; letter-spacing:-.5px; }
    .eyebrow { font-size:10px; font-weight:700; letter-spacing:2px;
      color:var(--muted); margin:0 0 5px; text-transform:uppercase; }
    .connection { font-size:12px; white-space:nowrap; color:var(--muted); }
    .dot { display:inline-block; width:7px; height:7px; border-radius:50%;
      background:#a4afb4; margin-right:6px; }
    .connected .dot { background:#1a9a68; }
    .remote { border:1px solid #d7e1e5; border-radius:28px; padding:22px;
      background:var(--paper); box-shadow:0 16px 40px #1939440c; }
    .screen { padding:18px 20px; border-radius:17px; background:#e7f0ed;
      border:1px solid #d7e4df; min-height:160px; }
    .screen-top { display:flex; justify-content:space-between;
      align-items:center; font-size:12px; font-weight:600; }
    .power-state { background:#d5e0dc; padding:4px 9px; border-radius:20px; }
    .power-state.on { background:#c3e7d6; color:#146343; }
    .temperature { display:flex; justify-content:center; align-items:start;
      margin:10px 0 6px; }
    #temperature { font-size:64px; font-weight:500; line-height:1;
      letter-spacing:-4px; font-variant-numeric:tabular-nums; }
    #units { font-size:21px; margin:7px 0 0 5px; }
    .screen-bottom { display:flex; justify-content:space-between;
      font-size:12px; color:#4a6963; }
    .flags { margin-top:10px; font-size:11px; text-align:center;
      min-height:15px; color:#4a6963; }
    .setpoint-label { text-align:center; margin-top:10px; font-size:10px;
      color:#4a6963; }
    .measured { display:flex; justify-content:space-between; align-items:center;
      gap:10px; margin-top:14px; padding-top:12px; border-top:1px solid #cbdcd5;
      font-size:12px; color:#4a6963; }
    #measured-temperature { font-size:22px; font-weight:600;
      font-variant-numeric:tabular-nums; white-space:nowrap; }
    #sensor-status { margin-top:3px; font-size:10px; }
    button { font:inherit; color:var(--ink); cursor:pointer; touch-action:manipulation;
      border:1px solid var(--line); background:white; transition:background .15s; }
    button:hover { background:#f0f6f7; }
    button:active { transform:translateY(1px); }
    button:focus-visible { outline:3px solid #087d8c66; outline-offset:3px; }
    button:disabled { cursor:default; opacity:.45; transform:none; }
    .main-buttons { display:flex; align-items:center; justify-content:center;
      gap:18px; margin:22px 0; }
    .temperature-button { width:64px; height:56px; border-radius:18px;
      font-size:28px; font-weight:400; }
    .power-button { width:76px; height:76px; border-radius:50%; border:0;
      background:var(--ink); color:white; font-size:11px; }
    .power-button:hover { background:#254f5d; }
    .power-button.active { background:var(--accent); }
    .power-button svg { display:block; width:24px; height:24px; margin:0 auto 5px; }
    .grid { display:grid; grid-template-columns:repeat(3,minmax(0,1fr)); gap:10px; }
    .key { min-height:76px; border-radius:16px; padding:11px 4px 9px;
      font-size:11px; display:flex; flex-direction:column; align-items:center;
      justify-content:center; gap:6px; }
    .key-icon { font-size:21px; line-height:1; font-weight:500; }
    .key.active { background:#eaf6f6; border-color:#89bcc3; color:#076f7e; }
    .key-value { font-size:10px; color:var(--muted); }
    #feedback { min-height:36px; margin:17px 0 0; font-size:12px;
      text-align:center; color:var(--muted); line-height:1.5; }
    #feedback.error { color:#b23c3c; }
    .send-again { display:block; margin:0 auto; border:0; padding:8px 12px;
      font-size:12px; color:var(--accent); background:transparent; }
    footer { margin:17px 8px; text-align:center; font-size:11px;
      line-height:1.6; color:var(--muted); }
    #address { display:block; margin-top:6px; color:#49636d; }
    @media(max-width:360px) { main { padding:0 10px; } .remote { padding:16px; }
      .main-buttons { gap:12px; } }
  </style>
</head>
<body>
  <main>
    <header>
      <div><p class="eyebrow">AC remote</p><h1>Blue Star</h1></div>
      <div id="connection" class="connection" role="status">
        <span class="dot"></span><span id="connection-text">Connecting</span>
      </div>
    </header>
    <section class="remote" aria-label="Air conditioner remote">
      <div class="screen" aria-label="Controller settings">
        <div class="screen-top">
          <span id="mode">Cool</span><span id="power-state" class="power-state">Off</span>
        </div>
        <div class="setpoint-label">Set temperature</div>
        <div class="temperature"><span id="temperature">--</span><span id="units">°C</span></div>
        <div class="screen-bottom"><span id="fan">Fan: --</span><span id="capacity">100% power</span></div>
        <div id="flags" class="flags">Controller settings</div>
        <div class="measured">
          <div>Measured temperature<div id="sensor-status">Waiting for sensor</div></div>
          <span id="measured-temperature" aria-live="polite">--</span>
        </div>
      </div>
      <div class="main-buttons">
        <button class="temperature-button" data-command="temp down" aria-label="Lower temperature">−</button>
        <button id="power" class="power-button" data-command="power toggle" aria-pressed="false">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true">
            <path d="M12 2v10M6 5.5a9 9 0 1 0 12 0" stroke-linecap="round"/>
          </svg>Power
        </button>
        <button class="temperature-button" data-command="temp up" aria-label="Raise temperature">+</button>
      </div>
      <div class="grid">
        <button class="key" data-command="mode next"><span class="key-icon" aria-hidden="true">❄</span>Mode<span id="mode-value" class="key-value">Cool</span></button>
        <button class="key" data-command="fan next"><span class="key-icon" aria-hidden="true">≋</span>Fan<span id="fan-value" class="key-value">Low</span></button>
        <button class="key" data-command="capacity next"><span class="key-icon" aria-hidden="true">5:1</span>Convertible<span id="capacity-value" class="key-value">100%</span></button>
        <button id="horizontal" class="key" data-command="horizontal toggle" aria-pressed="false"><span class="key-icon" aria-hidden="true">↔</span>Horizontal<span class="key-value">Swing</span></button>
        <button id="vertical" class="key" data-command="vertical toggle" aria-pressed="false"><span class="key-icon" aria-hidden="true">↕</span>Vertical<span class="key-value">Swing</span></button>
        <button id="turbo" class="key" data-command="turbo toggle" aria-pressed="false"><span class="key-icon" aria-hidden="true">ϟ</span>Turbo<span class="key-value">Boost</span></button>
        <button id="econo" class="key" data-command="econo toggle" aria-pressed="false"><span class="key-icon" aria-hidden="true">◇</span>Energy Save<span class="key-value">Eco</span></button>
        <button id="sleep" class="key" data-command="sleep toggle" aria-pressed="false"><span class="key-icon" aria-hidden="true">☾</span>Sleep<span class="key-value">Night</span></button>
        <button id="light" class="key" data-command="light toggle" aria-pressed="false"><span class="key-icon" aria-hidden="true">☼</span>Light<span class="key-value">AC display</span></button>
      </div>
      <p id="feedback" role="status" aria-live="polite">Loading controller settings…</p>
      <button class="send-again" data-command="send">Send again</button>
    </section>
    <footer>
      Settings reflect this controller. Changes made with the physical remote<br>
      are not received here.
      <span id="address"></span>
    </footer>
  </main>
  <script>
    const $ = id => document.getElementById(id);
    const buttons = [...document.querySelectorAll('[data-command]')];
    const modes = {1:'Fan only',2:'Auto',4:'Dry',8:'Cool'};
    const fans = {0:'High',1:'High',2:'Medium',4:'Low',7:'Auto'};
    const capacities = {1:110,2:80,3:60,4:40,5:100};
    let state = null;
    let busy = false;

    function updateButtons() {
      for (const button of buttons) {
        const command = button.dataset.command;
        let disabled = busy || state === null;
        if (state && command.startsWith('temp ')) {
          disabled ||= state.mode === 1;
          disabled ||= command === 'temp up' && state.temperature >= 30;
          disabled ||= command === 'temp down' && state.temperature <= 16;
        }
        button.disabled = disabled;
      }
    }

    function render(next) {
      state = next;
      const mode = modes[state.mode] || 'Unknown';
      const fan = state.turbo ? 'High' : (fans[state.fan] || 'Unknown');
      const capacity = capacities[state.capacity];
      $('mode').textContent = mode;
      $('mode-value').textContent = mode;
      $('temperature').textContent = state.mode === 1 ? '--' :
        String(state.turbo ? 16 : state.temperature);
      $('units').textContent = state.mode === 1 ? '' : '°C';
      const sensorReady = state.sensorStatus === 'ready' &&
        typeof state.measuredTemperature === 'number' &&
        Number.isFinite(state.measuredTemperature);
      $('measured-temperature').textContent = sensorReady ?
        state.measuredTemperature.toFixed(2) + ' °C' : '--';
      $('sensor-status').textContent = sensorReady ? 'DS18B20 · Updates every 5s' :
        (state.sensorStatus === 'waiting' ? 'Waiting for first reading' : 'Sensor unavailable');
      $('fan').textContent = 'Fan: ' + fan;
      $('fan-value').textContent = fan;
      $('capacity').textContent = capacity ? capacity + '% power' : 'Power: --';
      $('capacity-value').textContent = capacity ? capacity + '%' : '--';
      $('power-state').textContent = state.power ? 'On' : 'Off';
      $('power-state').classList.toggle('on', state.power);
      for (const flag of ['power','horizontal','vertical','turbo','econo','sleep','light']) {
        $(flag).classList.toggle('active', state[flag]);
        $(flag).setAttribute('aria-pressed', String(state[flag]));
      }
      const flags = [];
      if (state.turbo) flags.push('Turbo');
      if (state.econo) flags.push('Energy Save');
      if (state.sleep) flags.push('Sleep');
      if (state.horizontal || state.vertical) flags.push('Swing');
      $('flags').textContent = flags.join(' · ') || 'Controller settings';
      $('address').textContent = state.hostname + '.local · ' + state.ip;
      $('connection').classList.add('connected');
      $('connection-text').textContent = 'Connected';
      updateButtons();
    }

    async function request(command) {
      if (busy) return;
      busy = true;
      updateButtons();
      const abort = new AbortController();
      const timeout = setTimeout(() => abort.abort(), 5000);
      try {
        const options = {cache:'no-store', signal:abort.signal};
        if (command) {
          options.method = 'POST';
          options.headers = {'Content-Type':'application/x-www-form-urlencoded'};
          options.body = new URLSearchParams({command}).toString();
          $('feedback').textContent = 'Sending…';
        }
        const response = await fetch(command ? '/api/command' : '/api/state', options);
        const next = await response.json();
        if (typeof next.power !== 'boolean') throw new Error('Invalid controller response');
        const reconnecting = state === null;
        render(next);
        if (!response.ok || !next.ok) {
          $('feedback').textContent = next.message || 'Command could not be sent.';
          $('feedback').classList.add('error');
        } else if (command || !state.sent || reconnecting) {
          $('feedback').textContent = next.message || 'Ready. Choose a setting to send.';
          $('feedback').classList.remove('error');
        }
      } catch (error) {
        $('measured-temperature').textContent = '--';
        $('sensor-status').textContent = 'Controller offline';
        $('connection').classList.remove('connected');
        $('connection-text').textContent = 'Disconnected';
        $('feedback').textContent = command ?
          'Connection interrupted. Check the AC before retrying.' :
          'Controller unreachable. Reconnecting…';
        $('feedback').classList.add('error');
        state = null;
      } finally {
        clearTimeout(timeout);
        busy = false;
        updateButtons();
      }
    }

    for (const button of buttons) {
      button.addEventListener('click', () => request(button.dataset.command));
    }
    updateButtons();
    request();
    setInterval(() => request(), 5000);
  </script>
</body>
</html>
)HTML";

#endif  // WEB_UI_H_
