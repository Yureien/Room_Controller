// Browser behavior checks using Node's built-ins; no downloaded dependencies.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const page = fs.readFileSync(path.join(__dirname, '../src/web_ui.h'), 'utf8');
const script = page.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = new Map();
const strokes = [], dots = [], intervals = [], events = {}, calls = [];
let moves = [], history = {interval:60, age:0, samples:[20, null, 21]};
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
    classList:{toggle() {}, add() {}, remove() {}},
    setAttribute() {}, addEventListener() {}, getContext() { return ctx; }
  });
  return elements.get(id);
}
const state = {ok:true, power:false, sensorStatus:'ready', measuredTemperature:20,
  sensorIntervalSeconds:30, lamp:{configured:true, connected:false, busy:false,
    ok:true, completed:0, color:'#ffffff', brightness:100, speed:50, solid:true}};
const document = {hidden:false, getElementById:element, querySelectorAll(selector) {
  return selector === '.lamp button, .lamp input, .lamp select' ?
    [element('lamp-connect'), element('lamp-disconnect')] : [];
  },
  addEventListener(name, callback) { events[name] = callback; }};
const sandbox = {document, window:{devicePixelRatio:1, addEventListener() {}},
  AbortController, URLSearchParams,
  setTimeout() { return 1; }, clearTimeout() {},
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
  assert.equal(element('lamp-connect').textContent, 'Connect');
  assert.equal(element('lamp-disconnect').disabled, true);
  assert.equal(element('lamp-link').textContent, 'Sleeping');
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
  assert.equal(calls.length, hiddenCalls + 2, 'Reopened tabs refresh state and history');

  history = {interval:60, age:0, samples:Array(1440).fill(25)};
  await run('requestHistory()');
  assert.match(element('history-status').textContent, /^1440 readings/);
  history = {interval:60, age:0, samples:[null, null]};
  await run('requestHistory()');
  assert.match(element('history-status').textContent, /^No readings yet/);
  history = {interval:60, age:0, samples:[24.5]};
  dots.length = 0;
  await run('requestHistory()');
  assert.equal(dots.length, 1, 'Show a single startup reading');
  history = {interval:60, age:0, samples:Array(1441).fill(25)};
  await run('requestHistory()');
  assert.match(element('history-status').textContent, /^History unavailable/);
  run('state.lamp.connected = true; renderLamp()');
  assert.equal(element('lamp-connect').textContent, 'Reconnect');
  assert.equal(element('lamp-disconnect').disabled, false);
  console.log('Web UI checks passed: history gaps/capacity, polling, and connection controls.');
})().catch(error => { console.error(error); process.exitCode = 1; });
