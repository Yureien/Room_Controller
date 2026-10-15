// History rendering owns no network requests. Optional chart failures stay here.
(() => {
  'use strict';
  const get = id => document.getElementById(id);
  const frame = get('history-plot');
  const canvas = get('temperature-chart');
  const host = get('history-enhanced');
  const summary = get('history-summary');
  const status = get('history-status');
  const readout = get('history-inspection');
  const rangeButtons = [...document.querySelectorAll('[data-history-hours]')];
  const clock = seconds => new Date(seconds * 1000).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});
  const pointClock = seconds => new Date(seconds * 1000).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit',second:'2-digit'});
  let cached = null, received = 0, hours = 24, stale = false;
  let dirty = true, pendingFrame = 0, plot = null, libraryReady = false, libraryFailed = false;
  let selected = -1, geometry = null;
  let view = {x:[], y:[], count:0, low:20, high:30, min:0, max:0, start:0, end:1};

  // age is the last RETURNED sample's age, not the newest ring sample's age.
  // Anchor ranges at the ring end even when only old backfill pages are cached.
  function project() {
    const x = [], y = [], isolated = [];
    let count = 0, min = Infinity, max = -Infinity;
    if (!cached || !cached.total) return {x,y,isolated,count,low:20,high:30,start:0,end:1};
    const step = cached.interval;
    const returned = received / 1000 - cached.age;
    const newest = returned + (cached.end - cached.next) * step;
    const start = Math.max(newest - hours * 3600, newest - (cached.total - 1) * step);
    const oldest = returned - (cached.samples.length - 1) * step;
    const first = Math.max(0, Math.ceil((start - oldest) / step - 0.00001));
    for (let i = first; i < cached.samples.length; ++i) {
      const value = cached.samples[i];
      x.push(oldest + i * step);
      y.push(value);
      if (Number.isFinite(value)) {
        if ((i === first || !Number.isFinite(cached.samples[i - 1])) &&
            !Number.isFinite(cached.samples[i + 1])) isolated.push(x.length - 1);
        ++count;
        if (value < min) min = value;
        if (value > max) max = value;
      }
    }
    const pad = count ? Math.max(.5, (max - min) * .15) : 1;
    return {x,y,isolated,count,min,max,low:count ? min - pad : 20,high:count ? max + pad : 30,
      start:start === newest ? newest - step : start, end:newest};
  }

  // Short post-boot windows need seconds to distinguish adjacent samples.
  const axisClock = seconds => (view.end - view.start < 120 ? pointClock : clock)(seconds);

  function colors() {
    const css = getComputedStyle(document.documentElement);
    return name => css.getPropertyValue('--' + name).trim();
  }
  function messages() {
    summary.textContent = view.count ? view.count.toLocaleString() + (view.count === 1 ? ' reading · ' : ' readings · ') +
      (view.min === view.max ? view.min.toFixed(2) : view.min.toFixed(2) + '–' + view.max.toFixed(2)) + ' °C' :
      cached && cached.more && !view.x.length ? 'Selected range is still loading' : 'No measured readings in this range yet';
    if (stale) status.textContent = cached ? 'History unavailable. Showing cached readings.' : 'History unavailable. Waiting to reconnect.';
    else if (!cached) status.textContent = 'Loading temperature history…';
    else if (cached.more) status.textContent = view.x.length ? 'Loading history…' : 'Loading the selected range…';
    else status.textContent = view.x.length ? 'Last sample ' + clock(view.x[view.x.length - 1]) +
      ' · Last ' + hours + (hours === 1 ? ' hour' : ' hours') : 'No readings yet. Check the sensor.';
    canvas.setAttribute('aria-label', summary.textContent + '. ' + status.textContent);
    if (selected >= view.x.length) selected = -1;
    inspect(selected);
  }
  function inspect(index) {
    selected = index;
    readout.textContent = index >= 0 && index < view.x.length ? pointClock(view.x[index]) + ' · ' +
      (Number.isFinite(view.y[index]) ? view.y[index].toFixed(2) + ' °C' : 'No reading') :
      'Hover, touch, or use arrow keys to inspect';
  }

  function basic(width, height, color) {
    const scale = window.devicePixelRatio || 1;
    canvas.width = Math.round(width * scale);
    canvas.height = Math.round(height * scale);
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.scale(scale, scale);
    ctx.font = '11px system-ui';
    ctx.fillStyle = color('muted');
    const left = 43, right = width - 10, top = 12, bottom = height - 30;
    const x = time => left + (time - view.start) * (right - left) / (view.end - view.start);
    const y = value => bottom - (value - view.low) * (bottom - top) / (view.high - view.low);
    geometry = {left,right};
    if (!view.count) {
      ctx.fillText(cached && cached.more ? 'Loading selected range…' : 'Waiting for measured temperatures', 6, height / 2);
      return;
    }
    for (let i = 0; i < 3; i++) {
      const value = view.low + (view.high - view.low) * i / 2;
      ctx.strokeStyle = color('chart-grid');
      ctx.beginPath(); ctx.moveTo(left, y(value)); ctx.lineTo(right, y(value)); ctx.stroke();
      ctx.fillText(value.toFixed(1) + '°', 2, y(value) + 4);
    }
    ctx.strokeStyle = color('chart-line'); ctx.lineWidth = 2;
    ctx.beginPath();
    let joined = false;
    for (let i = 0; i < view.x.length; ++i) {
      if (!Number.isFinite(view.y[i])) { joined = false; continue; }
      if (joined) ctx.lineTo(x(view.x[i]), y(view.y[i]));
      else ctx.moveTo(x(view.x[i]), y(view.y[i]));
      joined = true;
    }
    ctx.stroke();
    ctx.fillStyle = color('chart-line');
    for (let i = 0; i < view.x.length; ++i) {
      if (!Number.isFinite(view.y[i]) || (view.x.length > 256 &&
          (Number.isFinite(view.y[i - 1]) || Number.isFinite(view.y[i + 1])))) continue;
      ctx.beginPath(); ctx.arc(x(view.x[i]), y(view.y[i]), 2, 0, Math.PI * 2); ctx.fill();
    }
    ctx.fillStyle = color('muted');
    ctx.fillText(axisClock(view.start), left, height - 8);
    ctx.textAlign = 'right'; ctx.fillText(axisClock(view.end), right, height - 8);
  }

  function useBasic() {
    host.hidden = true;
    canvas.hidden = false;
    get('chart-mode').textContent = 'Basic chart';
  }
  function abandonEnhancement() {
    libraryFailed = true;
    libraryReady = false;
    try { if (plot) plot.destroy(); } catch (_) {}
    plot = null;
    host.replaceChildren();
    useBasic();
  }
  function createPlot(width, height, color) {
    return new window.uPlot({
      width, height, legend:{show:false}, select:{show:false},
      cursor:{y:false, drag:{x:false,y:false,setScale:false}, hover:{skip:[undefined]}},
      scales:{x:{time:true,range:() => [view.start,view.end]}, y:{range:() => [view.low,view.high]}},
      series:[{}, {label:'Temperature',spanGaps:false,width:2,
        stroke:() => colors()('chart-line'), fill:() => colors()('chart-fill'),
        points:{show:true,size:4,width:0,filter:() => view.x.length > 256 ? view.isolated : null}, value:(_,value) => value == null ? 'No reading' : value.toFixed(2) + ' °C'}],
      axes:[
        {stroke:color('muted'),grid:{stroke:color('chart-grid'),width:1},ticks:{show:false},
          font:'11px system-ui',space:70,values:(_,ticks) => ticks.map(axisClock)},
        {stroke:color('muted'),grid:{stroke:color('chart-grid'),width:1},ticks:{show:false},
          font:'11px system-ui',size:48,values:(_,ticks) => ticks.map(v => v.toFixed(1) + '°')}
      ],
      hooks:{setCursor:[self => { if (self.cursor.idx != null) inspect(self.cursor.idx); }]}
    }, [view.x, view.y], host);
  }
  function render() {
    pendingFrame = 0;
    if (document.hidden) return;
    try {
      if (dirty) { view = project(); dirty = false; selected = -1; }
      messages();
      const width = Math.floor(frame.clientWidth), height = Math.floor(frame.clientHeight);
      if (!width || !height) return;
      const color = colors();
      // Always prepare a local frame before attempting optional external code.
      basic(width, height, color);
      if (!libraryReady || libraryFailed || view.x.length < 2 || !view.count) { useBasic(); return; }
      host.hidden = false;
      try {
        if (!plot) plot = createPlot(width, height, color);
        else {
          plot.setSize({width,height});
          plot.setData([view.x,view.y]);
        }
        canvas.hidden = true;
        get('chart-mode').textContent = 'Interactive chart';
      } catch (_) { abandonEnhancement(); }
    } catch (_) {
      // A drawing failure must not escape into the history fetch/error path.
      abandonEnhancement();
      status.textContent = stale ? 'History unavailable. Showing cached readings.' : 'Chart unavailable. Readings will refresh.';
    }
  }
  function schedule() {
    if (!document.hidden && !pendingFrame) pendingFrame = requestAnimationFrame(render);
  }

  // Loaded once, after local startup. Both resources must load before promotion.
  function loadEnhancement() {
    const base = 'https://cdn.jsdelivr.net/npm/uplot@1.6.32/dist/';
    const script = document.createElement('script'), css = document.createElement('link');
    let scriptReady = false, styleReady = false, settled = false;
    const finish = failed => {
      if (settled) return;
      if (!failed && !(scriptReady && styleReady)) return;
      settled = true;
      clearTimeout(deadline);
      if (failed || typeof window.uPlot !== 'function') {
        script.remove(); css.remove(); abandonEnhancement();
      } else { libraryReady = true; schedule(); }
    };
    const deadline = setTimeout(() => finish(true), 5500);
    script.src = base + 'uPlot.iife.min.js'; script.async = true;
    script.integrity = 'sha384-Gx3t0zdBAuQOuvvmaLZj7HKEiSgWTAs+VdtNY7wt19QDPTDQjFIwAuXDj0zeN00c';
    css.rel = 'stylesheet'; css.href = base + 'uPlot.min.css';
    css.integrity = 'sha384-IfV0B7MIOYuO95kO9G5ySKPz/85zqFNOAs8iy4tkK5zd9izhJAB8b7lHrwYqqmYE';
    script.crossOrigin = css.crossOrigin = 'anonymous';
    script.onload = () => { scriptReady = true; finish(false); };
    css.onload = () => { styleReady = true; finish(false); };
    script.onerror = css.onerror = () => finish(true);
    document.head.append(css, script);
  }

  rangeButtons.forEach(button => button.addEventListener('click', () => {
    hours = Number(button.dataset.historyHours);
    rangeButtons.forEach(item => item.setAttribute('aria-pressed', String(item === button)));
    dirty = true; schedule();
  }));
  frame.addEventListener('keydown', event => {
    if (!['ArrowLeft','ArrowRight','Home','End'].includes(event.key) || !view.x.length) return;
    event.preventDefault();
    const last = view.x.length - 1;
    const next = event.key === 'Home' ? 0 : event.key === 'End' ? last :
      Math.max(0, Math.min(last, (selected < 0 ? last : selected) + (event.key === 'ArrowLeft' ? -1 : 1)));
    inspect(next);
    if (plot && !host.hidden) {
      try { plot.setCursor({left:plot.valToPos(view.x[next],'x'),top:0}); } catch (_) { abandonEnhancement(); schedule(); }
    }
  });
  const point = event => {
    if (!view.x.length) return;
    try {
      const area = plot && !host.hidden ? plot.over.getBoundingClientRect() : frame.getBoundingClientRect();
      const left = host.hidden && geometry ? geometry.left : 0;
      const right = host.hidden && geometry ? geometry.right : area.width;
      const fraction = Math.max(0, Math.min(1, (event.clientX - area.left - left) / (right - left)));
      const time = view.start + fraction * (view.end - view.start);
      const index = Math.max(0, Math.min(view.x.length - 1, Math.round((time - view.x[0]) / (cached ? cached.interval : 5))));
      inspect(index);
    } catch (_) { /* Point inspection cannot affect page scrolling or controls. */ }
  };
  frame.addEventListener('pointermove', point, {passive:true});
  frame.addEventListener('pointerdown', point, {passive:true});
  if (window.ResizeObserver) new ResizeObserver(schedule).observe(frame);
  window.addEventListener('resize', schedule);
  document.addEventListener('room-theme-change', () => {
    try { if (plot) plot.destroy(); } catch (_) {}
    plot = null; host.replaceChildren(); useBasic(); schedule();
  });
  document.addEventListener('visibilitychange', () => { if (!document.hidden) schedule(); });
  window.RoomHistory = {
    update(data, at) { cached = data; received = at; stale = false; dirty = true; schedule(); },
    unavailable() { stale = true; schedule(); }
  };
  schedule();
  if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', loadEnhancement, {once:true});
  else loadEnhancement();
})();
