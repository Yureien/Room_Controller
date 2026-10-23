// Current web/controller.js rendered against a small DOM, without real timers.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const elements = new Map();
function element(id) {
  if (!elements.has(id)) elements.set(id, {
    textContent:'', value:'', dataset:{}, attributes:{}, disabled:false,
    classList:{toggle() {}, add() {}, remove() {}},
    setAttribute(name, value) { this.attributes[name] = value; },
    addEventListener() {}
  });
  return elements.get(id);
}
const buttons = [['power','power toggle'], ['auto-cool','autocool toggle'],
  ['turbo','turbo toggle'], ['temp-down','temp down']].map(([id, command]) => {
    const button = element(id);
    button.dataset.command = command;
    return button;
  });
const sandbox = {
  document:{hidden:true, getElementById:element, addEventListener() {},
    querySelectorAll(selector) { return selector === '[data-command]' ? buttons : []; }},
  RoomHistory:{update() {}, unavailable() {}}, AbortController, URLSearchParams,
  setTimeout() {}, clearTimeout() {}, setInterval() {},
  fetch() { return new Promise(() => {}); }
};
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(path.join(__dirname, '../web/controller.js'), 'utf8'), sandbox);
let revision = 0;
const state = {power:true, session:'1234abcd', mode:8, fan:4, temperature:24,
  turbo:true, sensorStatus:'ready', measuredTemperature:27, sensorIntervalSeconds:5,
  autoCool:{enabled:false, target:24, waiting:false, recoveryUsed:false, saved:true}};
function render() {
  sandbox.next = {...state, revision:++revision};
  vm.runInContext('render(next)', sandbox);
}
render();
assert.equal(element('temperature').textContent, '16');
assert.equal(element('fan-value').textContent, 'High');
state.turbo = false;
state.turboCooldownSeconds = 300;
render();
assert.equal(element('temperature').textContent, '24');
assert.equal(element('fan-value').textContent, 'Low');
assert.equal(element('turbo').attributes['aria-pressed'], 'false');
state.autoCool.enabled = true;
render();
assert.match(element('auto-cool-status').textContent, /Normal cooling; Turbo available in 300s/);
state.turboCooldownSeconds = 0;
render();
assert.match(element('auto-cool-status').textContent, /Monitoring temperature/);
state.autoCool.waiting = true;
state.autoCool.waitSeconds = 120;
state.autoCool.recoveryAttempts = 2;
state.autoCool.recoveryBackoffSeconds = 600;
state.power = false;
render();
assert.match(element('auto-cool-status').textContent, /Restart in 120s/);
assert.match(element('auto-cool-status').textContent, /attempt 2/);
assert.equal(element('power').dataset.command, 'off');
assert.equal(element('power').disabled, false);
assert.equal(element('turbo').disabled, true);
state.autoCool.waiting = false;
state.power = true;
render();
assert.match(element('auto-cool-status').textContent, /next Off wait 10 min/);
state.autoCool.cancelled = true;
state.autoCool.waiting = false;
state.autoCool.enabled = false;
render();
assert.match(element('auto-cool-status').textContent, /Restart cancelled/);
console.log('AC UI checks passed: Turbo expiry display, cooldown, recovery, cancellation.');
