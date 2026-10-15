// Browser behavior checks using Node's built-ins; no downloaded dependencies.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const page = fs.readFileSync(path.join(__dirname, '../src/web_ui.h'), 'utf8');
const script = page.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = new Map();
const strokes = [], dots = [], intervals = [], events = {}, calls = [];
let moves = [];
function historyPage(samples, {start = 0, end = start + samples.length, first = 0,
    reset = true, session = '1234abcd'} = {}) {
  return {session, interval:5, age:(end - start - samples.length) * 5,
    first, start, next:start + samples.length, end, total:end - first,
    reset, more:start + samples.length < end, samples};
}
let history = historyPage([20, null, 21]);
const timers = new Map();
let timerId = 0;
const ctx = {
  scale() {}, fillText() {}, beginPath() { moves = []; },
  moveTo(x, y) { moves.push(['move', x, y]); },
  lineTo(x, y) { moves.push(['line', x, y]); },
  stroke() { strokes.push([...moves]); },
  arc(x, y) { dots.push([x, y]); }, fill() {}
};
function element(id) {
  if (!elements.has(id)) elements.set(id, {
    clientWidth:320, value:'50', textContent:'', disabled:false,
    dataset:{}, classList:{toggle() {}, add() {}, remove() {}},
    setAttribute() {}, addEventListener() {}, getContext() { return ctx; }
  });
  return elements.get(id);
}
const state = {ok:true, power:false, sensorStatus:'ready', measuredTemperature:20,
  mode:8, temperature:24, turbo:false,
  autoCool:{enabled:false, target:24, waiting:false, recoveryUsed:false, cancelled:false, saved:true},
  sensorIntervalSeconds:5, lamp:{configured:true, connected:false, busy:false,
    ok:true, completed:0, color:'#ffffff', brightness:100, speed:50, solid:true}};
const document = {hidden:false, getElementById:element, querySelectorAll(selector) {
  if (selector === '[data-command]') {
    for (const [id, command] of [['auto-cool','autocool toggle'], ['power','power toggle'],
         ['temp-up','temp up'], ['temp-down','temp down'], ['send','send']])
      element(id).dataset.command = command;
    return ['auto-cool','power','temp-up','temp-down','send'].map(element);
  }
  return selector === '.lamp button, .lamp input, .lamp select' ?
    [element('lamp-connect'), element('lamp-disconnect')] : [];
  },
  addEventListener(name, callback) { events[name] = callback; }};
const sandbox = {document, window:{devicePixelRatio:1, addEventListener() {}},
  AbortController, URLSearchParams,
  setTimeout(callback, delay) { timers.set(++timerId, {callback, delay}); return timerId; },
  clearTimeout(id) { timers.delete(id); },
  setInterval(callback, delay) { intervals.push({callback, delay}); },
  async fetch(url) {
    calls.push(url);
    return {ok:true, async json() { return url.includes('history') ? history : state; }};
  }};
vm.createContext(sandbox);
const run = code => vm.runInContext(code, sandbox);
const settle = () => new Promise(resolve => setImmediate(resolve));

(async () => {
  run(script);
  await settle();
  await run('requestHistory()');
  assert.deepEqual(intervals.map(interval => interval.delay), [5000]);
  assert([...timers.values()].some(timer => timer.delay === 5000),
    'Incremental history refreshes five seconds after completion');
  assert.equal(element('lamp-connect').textContent, 'Connect');
  assert.equal(element('lamp-disconnect').disabled, true);
  assert.equal(element('lamp-link').textContent, 'Sleeping');
  assert.equal(element('auto-cool').disabled, true, 'Auto-Cool cannot turn the AC on');
  assert.equal(dots.length, 2, 'Isolated readings must be visible');
  const plot = strokes[strokes.length - 1];
  assert.equal(plot.filter(p => p[0] === 'move').length, 2);
  assert.equal(plot.filter(p => p[0] === 'line').length, 0, 'Do not join a sensor outage');

  document.hidden = true;
  events.visibilitychange();
  const hiddenCalls = calls.length;
  for (const interval of intervals) interval.callback();
  await settle();
  assert.equal(calls.length, hiddenCalls, 'Hidden tabs must stop polling');

  document.hidden = false;
  events.visibilitychange();
  await settle();
  await run('requestHistory()');
  assert.equal(calls.length, hiddenCalls + 2, 'Reopened tabs refresh state and history');

  // Full-day backfill stays bounded per response and accumulates in the browser.
  for (let start = 0; start < 17280; start += 128) {
    history = historyPage(Array(Math.min(128, 17280 - start)).fill(25),
      {start, end:17280, reset:start === 0});
    await run('requestHistory()');
    if (start === 0) {
      assert.match(element('history-status').textContent, /Loading history/);
      assert([...timers.values()].some(timer => timer.delay === 100));
    }
  }
  assert.match(element('history-status').textContent, /^17280 readings/);
  history = historyPage([26], {start:17280, end:17281, first:1, reset:false});
  await run('requestHistory()');
  assert.equal(run('historyData.samples.length'), 17280);
  assert.equal(run('historyData.samples.at(-1)'), 26);
  assert.equal(run('historyData.start'), 1);
  assert.match(calls.at(-1), /session=1234abcd&cursor=17280/);
  history = historyPage([], {start:17281, first:1, reset:false});
  await run('requestHistory()');
  assert.equal(run('historyData.samples.length'), 17280, 'Empty incremental page retains the graph');

  history = historyPage([null, null], {session:'deadbeef'});
  await run('requestHistory()');
  assert.match(element('history-status').textContent, /^No readings yet/);
  assert.equal(run('historyData.samples.length'), 2, 'Reboot discards the old session');
  history = historyPage([24.5]);
  dots.length = 0;
  await run('requestHistory()');
  assert.equal(dots.length, 1, 'Show a single startup reading');
  history = historyPage(Array(129).fill(25));
  await run('requestHistory()');
  assert.match(element('history-status').textContent, /^History unavailable/);
  assert.equal(run('historyData.samples.length'), 1, 'Invalid pages do not destroy cached data');
  history = historyPage([26], {start:2, reset:false});
  await run('requestHistory()');
  assert.match(element('history-status').textContent, /^History unavailable/);
  history = historyPage([26], {start:10, first:10});
  await run('requestHistory()');
  assert.equal(run('historyData.start'), 10, 'Expired cursors restart at the oldest retained reading');
  const beforeBusy = calls.length;
  run('busy = true');
  await run('requestHistory()');
  run('busy = false');
  assert.equal(calls.length, beforeBusy, 'History yields to control requests');
  assert([...timers.values()].some(timer => timer.delay === 250));
  run('state.lamp.connected = true; renderLamp()');
  assert.equal(element('lamp-connect').textContent, 'Reconnect');
  assert.equal(element('lamp-disconnect').disabled, false);
  run('state.power = true; state.turbo = true; state.autoCool.enabled = true; render(state)');
  assert.equal(element('auto-cool').textContent, 'Auto-Cool: On');
  assert.equal(element('temperature').textContent, '24', 'Display the target during Turbo');
  assert.match(element('flags').textContent, /AC running at 16°C/);
  run('state.autoCool.target = 16; render(state)');
  assert.equal(element('temp-down').disabled, true);
  run('state.power = false; state.autoCool.waiting = true; state.autoCool.waitSeconds = 90; render(state)');
  assert.equal(element('power').dataset.command, 'off', 'Power button cancels a pending restart');
  assert.equal(element('power').disabled, false);
  assert.equal(element('auto-cool').disabled, false);
  assert.equal(element('temp-up').disabled, true);
  assert.equal(element('send').disabled, true);
  assert.match(element('auto-cool-status').textContent, /Restart in 90s/);
  run('state.autoCool.waiting = false; state.autoCool.enabled = false; state.autoCool.cancelled = true; render(state)');
  assert.match(element('auto-cool-status').textContent, /Restart cancelled/);
  assert.equal(element('power').dataset.command, 'power toggle');
  run('state.autoCool.saved = false; render(state)');
  assert.match(element('auto-cool-status').textContent, /could not be saved/);
  console.log('Web UI checks passed: history, polling, connection controls, and Auto-Cool recovery controls.');
})().catch(error => { console.error(error); process.exitCode = 1; });
