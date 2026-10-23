// Local firmware UI. Reads may be cancelled; commands are never replayed.
const $ = id => document.getElementById(id);
const buttons = [...document.querySelectorAll('[data-command]')];
const modes = {1:'Fan only',2:'Auto',4:'Dry',8:'Cool'};
const fans = {0:'High',1:'High',2:'Medium',4:'Low',7:'Auto'};
const capacities = {1:110,2:80,3:60,4:40,5:100};
let historyData = null;
let historyReceived = 0;
let historyLoading = false;
let historyTimer;
let historyAbort = null;
let stateAbort = null;
let successfulReplies = 0;
let currentSession = null;
let currentRevision = -1;
const retiredSessions = [];

// A command gets the browser's connection and radio budget immediately.
// Cancelling a read is safe; commands are never cancelled or replayed here.
function cancelBackgroundReads() {
  if (historyAbort) { historyAbort.abort(); historyAbort = null; }
  if (stateAbort) { stateAbort.abort(); stateAbort = null; }
}

function drawHistory() {
  RoomHistory.update(historyData, historyReceived);
}

function scheduleHistory(delay) {
  clearTimeout(historyTimer);
  if (!document.hidden) historyTimer = setTimeout(requestHistory, delay);
}

async function requestHistory() {
  if (historyLoading || document.hidden) return;
  if (busy || stateLoading || lampSending) { scheduleHistory(250); return; }
  clearTimeout(historyTimer);
  historyLoading = true;
  const abort = new AbortController();
  historyAbort = abort;
  const timeout = setTimeout(() => abort.abort(), 5000);
  let more = false;
  try {
    const query = new URLSearchParams({limit:128});
    if (historyData) {
      query.set('session', historyData.session);
      query.set('cursor', historyData.next);
    }
    const response = await fetch('/api/temperature/history?' + query, {cache:'no-store',signal:abort.signal});
    const next = await response.json();
    if (historyAbort !== abort || document.hidden) return;
    if (!response.ok || !Array.isArray(next.samples) || next.samples.length > 128 ||
        !/^[0-9a-f]{8}$/.test(next.session) || next.interval !== 5 ||
        !Number.isFinite(next.age) || next.age < 0 ||
        !['first','start','next','end','total'].every(key => Number.isSafeInteger(next[key]) && next[key] >= 0) ||
        next.total > 17280 || next.end - next.first !== next.total ||
        next.start < next.first || next.next < next.start || next.next > next.end ||
        next.next - next.start !== next.samples.length ||
        typeof next.reset !== 'boolean' || next.more !== (next.next < next.end) ||
        (next.more && !next.samples.length) ||
        !next.samples.every(v => v === null || (Number.isFinite(v) && v >= -55 && v <= 125)))
      throw new Error('Invalid history');
    if (historyData && !next.reset) {
      if (next.session !== historyData.session || next.start !== historyData.next)
        throw new Error('Discontinuous history');
      const start = Math.max(historyData.start, next.first);
      next.samples = historyData.samples.concat(next.samples).slice(start - historyData.start);
      next.start = start;
    } else if (next.start !== next.first) throw new Error('Invalid history reset');
    historyData = next;
    historyReceived = Date.now();
    more = next.more;
    drawHistory();
  } catch (error) {
    if (historyAbort === abort)
      RoomHistory.unavailable();
  } finally {
    clearTimeout(timeout);
    historyAbort = null;
    historyLoading = false;
    // One request at a time, with room for commands between backfill pages.
    scheduleHistory(more ? 100 : 5000);
  }
}
let state = null;
let busy = false;
let stateLoading = false;
let stateGeneration = 0;
let lampSending = false;
let lampPollTimer;
let lampLocalError = '';
let lampPendingEdit = null;
const lampEdited = new Set();
const lampControls = [...document.querySelectorAll('.lamp button, .lamp input, .lamp select')];

function updateLampControls() {
  const lamp = state && state.lamp;
  for (const control of lampControls) {
    control.disabled = lampSending || !lamp || !lamp.configured || lamp.busy;
  }
  $('lamp-disconnect').disabled ||= !lamp || !lamp.connected;
}

function renderLamp() {
  const lamp = state.lamp;
  if (!lamp) return;
  if (lampPendingEdit && lamp.completed > lampPendingEdit.completed) {
    if (lamp.ok) lampEdited.delete(lampPendingEdit.field);
    lampPendingEdit = null;
  }
  $('lamp-link').textContent = lamp.busy ? 'Working…' :
    !lamp.configured ? 'Not set up' : lamp.connected ? 'Connected' : 'Sleeping';
  $('lamp-connect').textContent = lamp.connected ? 'Reconnect' : 'Connect';
  $('lamp-name').textContent = lamp.configured ? (lamp.name || 'LED strip') + ' · ' + lamp.address :
    'Select your strip over serial.';
  for (const [id, value] of [['lamp-color',lamp.color],['lamp-brightness',lamp.brightness],
                           ['lamp-speed',lamp.speed],['lamp-effect',lamp.solid ? -1 : lamp.effect]]) {
    if (!lampEdited.has(id)) $(id).value = value;
  }
  for (const field of ['brightness','speed']) {
    $('lamp-' + field + '-value').textContent = $('lamp-' + field).value + '%';
  }
  for (const [id, active] of [['lamp-on',lamp.power === true],['lamp-off',lamp.power === false]]) {
    $(id).classList.toggle('active', active);
    $(id).setAttribute('aria-pressed', String(active));
  }
  for (const button of document.querySelectorAll('.swatch')) {
    const selected = lamp.solid && button.dataset.lampCommand === 'color ' + lamp.color;
    button.classList.toggle('selected', selected);
    button.setAttribute('aria-pressed', String(selected));
  }
  $('lamp-feedback').textContent = lampLocalError || lamp.message;
  $('lamp-feedback').classList.toggle('error', Boolean(lampLocalError) || !lamp.ok);
  clearTimeout(lampPollTimer);
  if (lamp.busy && !document.hidden) lampPollTimer = setTimeout(() => request(), 750);
  updateLampControls();
}

async function requestLamp(command, field = null) {
  if (lampSending || !state || !state.lamp || state.lamp.busy) return;
  lampSending = true;
  ++stateGeneration;
  cancelBackgroundReads();
  lampLocalError = '';
  updateLampControls();
  const abort = new AbortController();
  const sessionAtStart = currentSession;
  const timeout = setTimeout(() => abort.abort(), 5000);
  try {
    const response = await fetch('/api/lamp/command', {
      method:'POST',cache:'no-store',signal:abort.signal,
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:new URLSearchParams({command}).toString()
    });
    const next = await response.json();
    if (!response.ok && !validState(next) && next.ok === false && typeof next.message === 'string') {
      if (currentSession !== sessionAtStart) return;
      lampLocalError = next.message;
      $('lamp-feedback').textContent = lampLocalError;
      $('lamp-feedback').classList.add('error');
      return;
    }
    if (!validState(next) || !next.lamp) throw new Error('Invalid controller response');
    if (retiredSessions.includes(next.session) ||
        (next.session === currentSession && next.revision < currentRevision)) return;
    if (!response.ok || !next.ok) lampLocalError = next.message || 'Lamp command rejected.';
    else if (field) lampPendingEdit = {field, completed:next.lamp.completed, session:next.session};
    if (!render(next)) return;
  } catch (error) {
    if (currentSession !== sessionAtStart) return;
    lampLocalError = 'Connection interrupted. Check the strip before retrying.';
    $('lamp-feedback').textContent = lampLocalError;
    $('lamp-feedback').classList.add('error');
  } finally {
    clearTimeout(timeout);
    lampSending = false;
    updateLampControls();
  }
}

function updateButtons() {
  for (const button of buttons) {
    const command = button.dataset.command;
    let disabled = busy || state === null;
    const auto = state && state.autoCool;
    if (auto && auto.waiting) {
      disabled ||= command !== 'off' && command !== 'autocool toggle';
    }
    if (state && command === 'autocool toggle' && !(auto && auto.enabled)) {
      disabled ||= !state.power || state.mode !== 8 || state.econo || state.sleep || state.sensorStatus !== 'ready' ||
        !Number.isFinite(state.measuredTemperature);
    }
    if (state && command.startsWith('temp ')) {
      const target = auto && auto.enabled ? auto.target : state.temperature;
      disabled ||= state.mode === 1;
      disabled ||= command === 'temp up' && target >= 30;
      disabled ||= command === 'temp down' && target <= 16;
    }
    button.disabled = disabled;
  }
  updateLampControls();
}

function validState(next) {
  return typeof next.power === 'boolean' && /^[0-9a-f]{8}$/.test(next.session) &&
    Number.isSafeInteger(next.revision) && next.revision >= 0;
}

function render(next) {
  ++successfulReplies;
  // Concurrent requests may arrive out of order. Session changes after a
  // restart allow revisions to start again at one.
  if (next.session === currentSession && next.revision < currentRevision) return false;
  if (retiredSessions.includes(next.session)) return false;
  if (currentSession && next.session !== currentSession) {
    retiredSessions.push(currentSession);
    if (retiredSessions.length > 4) retiredSessions.shift();
    if (lampPendingEdit && lampPendingEdit.session !== next.session) lampPendingEdit = null;
    lampLocalError = '';
    historyData = null;
    drawHistory();
  }
  currentSession = next.session;
  currentRevision = next.revision;
  state = next;
  renderLamp();
  const mode = modes[state.mode] || 'Unknown';
  const fan = state.turbo ? 'High' : (fans[state.fan] || 'Unknown');
  const capacity = capacities[state.capacity];
  const auto = state.autoCool;
  const autoEnabled = auto && auto.enabled;
  $('auto-cool').textContent = autoEnabled ? 'Auto-Cool: On' : 'Auto-Cool: Off';
  $('auto-cool').classList.toggle('active', Boolean(autoEnabled));
  $('auto-cool').setAttribute('aria-pressed', String(Boolean(autoEnabled)));
  $('auto-cool-status').textContent = auto && auto.cancelled ?
    'Sensor lost during recovery. Restart cancelled; turn the AC on manually.' :
    auto && auto.waiting ? 'Recovery: AC off. Restart in ' + auto.waitSeconds +
      's (attempt ' + auto.recoveryAttempts + '). Power or Auto-Cool cancels the restart.' :
    autoEnabled ? 'Target ' + auto.target + '°C · ' +
      (!Number.isFinite(state.measuredTemperature) || state.sensorStatus !== 'ready' ?
        'Paused: sensor unavailable.' : state.turbo ? 'Turbo cooling (20-minute limit).' :
        state.turboCooldownSeconds ? 'Normal cooling; Turbo available in ' +
          state.turboCooldownSeconds + 's.' : 'Monitoring temperature.') +
      (auto.recoveryAttempts ? ' Recovery attempts: ' + auto.recoveryAttempts +
        '; next Off wait ' + auto.recoveryBackoffSeconds / 60 + ' min.' : '') :
    'Enable with AC on in Cool mode, Energy Save and Sleep off. Auto-Cool and its target are saved across restarts. Automatic signals send 3 times, 2 seconds apart.';
  if (auto && auto.saved === false)
    $('auto-cool-status').textContent += ' Settings could not be saved to flash.';
  $('power').dataset.command = auto && auto.waiting ? 'off' : 'power toggle';
  $('mode').textContent = mode;
  $('mode-value').textContent = mode;
  $('temperature').textContent = state.mode === 1 ? '--' :
    String(autoEnabled ? auto.target : state.turbo ? 16 : state.temperature);
  $('units').textContent = state.mode === 1 ? '' : '°C';
  const sensorReady = state.sensorStatus === 'ready' &&
    typeof state.measuredTemperature === 'number' &&
    Number.isFinite(state.measuredTemperature);
  $('measured-temperature').textContent = sensorReady ?
    state.measuredTemperature.toFixed(2) + ' °C' : '--';
  $('sensor-status').textContent = sensorReady ? 'DS18B20 · Updates every ' + state.sensorIntervalSeconds + 's' :
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
  if (autoEnabled && state.turbo) flags.push('AC running at 16°C');
  if (state.econo) flags.push('Energy Save');
  if (state.sleep) flags.push('Sleep');
  if (state.horizontal || state.vertical) flags.push('Swing');
  $('flags').textContent = flags.join(' · ') || 'Controller settings';
  $('address').textContent = state.hostname + '.local · ' + state.ip;
  $('connection').classList.add('connected');
  $('connection-text').textContent = 'Connected';
  updateButtons();
  return true;
}

async function request(command) {
  if (busy || (!command && (stateLoading || lampSending))) return;
  if (command) {
    busy = true;
    cancelBackgroundReads();
  }
  else stateLoading = true;
  const generation = ++stateGeneration;
  const repliesAtStart = successfulReplies;
  const sessionAtStart = currentSession;
  updateButtons();
  const abort = new AbortController();
  if (!command) stateAbort = abort;
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
    if (!command && (generation !== stateGeneration || stateAbort !== abort || document.hidden)) return;
    if (!response.ok && !validState(next) && next.ok === false && typeof next.message === 'string') {
      if (currentSession !== sessionAtStart) return;
      $('feedback').textContent = next.message;
      $('feedback').classList.add('error');
      return;
    }
    if (!validState(next)) throw new Error('Invalid controller response');
    const reconnecting = state === null;
    if (!render(next)) return;
    if (!response.ok || !next.ok) {
      $('feedback').textContent = next.message || 'Command could not be sent.';
      $('feedback').classList.add('error');
    } else if (command || !state.sent || reconnecting) {
      $('feedback').textContent = next.message || 'Ready. Choose a setting to send.';
      $('feedback').classList.remove('error');
    }
  } catch (error) {
    if (!command && (generation !== stateGeneration || stateAbort !== abort || document.hidden)) return;
    // Another request can succeed while this one times out. Preserve that
    // newer evidence of reachability and report this command's uncertainty.
    if (successfulReplies !== repliesAtStart) {
      if (command) {
        $('feedback').textContent = 'Command response lost. Check the AC before retrying.';
        $('feedback').classList.add('error');
      }
      return;
    }
    $('measured-temperature').textContent = '--';
    $('sensor-status').textContent = 'Controller offline';
    $('lamp-link').textContent = 'Controller offline';
    clearTimeout(lampPollTimer);
    $('connection').classList.remove('connected');
    $('connection-text').textContent = 'Disconnected';
    $('feedback').textContent = command ?
      'Connection interrupted. Check the AC before retrying.' :
      'Controller unreachable. Reconnecting…';
    $('feedback').classList.add('error');
    state = null;
  } finally {
    clearTimeout(timeout);
    if (command) busy = false;
    else { stateLoading = false; stateAbort = null; }
    updateButtons();
  }
}

for (const button of buttons) {
  button.addEventListener('click', () => request(button.dataset.command));
}
for (const button of document.querySelectorAll('[data-lamp-command]')) {
  button.addEventListener('click', () => requestLamp(button.dataset.lampCommand));
}
for (const id of ['lamp-color','lamp-brightness','lamp-speed','lamp-effect']) {
  $(id).addEventListener('input', () => {
    lampEdited.add(id);
    if (id === 'lamp-brightness' || id === 'lamp-speed') {
      $(id + '-value').textContent = $(id).value + '%';
    }
  });
}
$('lamp-color-apply').addEventListener('click', () =>
  requestLamp('color ' + $('lamp-color').value, 'lamp-color'));
for (const field of ['brightness','speed']) {
  $('lamp-' + field + '-apply').addEventListener('click', () =>
    requestLamp(field + ' ' + $('lamp-' + field).value, 'lamp-' + field));
}
$('lamp-effect-apply').addEventListener('click', () => {
  const effect = $('lamp-effect').value;
  requestLamp(effect === '-1' ? 'color ' + $('lamp-color').value : 'effect ' + effect, 'lamp-effect');
});
updateButtons();
request();
requestHistory();
setInterval(() => { if (!document.hidden) request(); }, 5000);

document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    cancelBackgroundReads();
    clearTimeout(lampPollTimer);
    clearTimeout(historyTimer);
  }
  else { request(); requestHistory(); }
});
