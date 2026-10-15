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
  const tooltip = get('history-tooltip');
  const rangeButtons = [...document.querySelectorAll('[data-history-hours]')];
  const clock = seconds => new Date(seconds * 1000).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});
  const pointClock = seconds => new Date(seconds * 1000).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit',second:'2-digit'});
  let cached = null, received = 0, hours = 24, stale = false;
  let dirty = true, pendingFrame = 0, plot = null, libraryReady = false, libraryFailed = false;
  let selected = -1, selectedTime = null, pointer = null, geometry = null;
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
  }
  function dismiss() {
    selected = -1;
    selectedTime = pointer = null;
    tooltip.hidden = true;
    frame.setAttribute('aria-describedby', 'history-summary history-inspection');
    readout.textContent = 'Hover, touch, or use arrow keys to inspect';
    try { if (plot) plot.setCursor({left:-10,top:-10}); } catch (_) {}
  }
  function plotBounds() {
    if (plot && !host.hidden) return plot.over.getBoundingClientRect();
    if (!geometry) return null;
    const rect = frame.getBoundingClientRect();
    return {left:rect.left + geometry.left, top:rect.top + geometry.top,
      width:geometry.right - geometry.left, height:geometry.bottom - geometry.top};
  }
  function indexAtTime(time) {
    if (!view.x.length) return -1;
    const step = cached ? cached.interval : 5;
    // Unfetched backfill and expired points do not become boundary readings.
    if (time < view.x[0] - step / 2 || time > view.x[view.x.length - 1] + step / 2) return -1;
    return Math.max(0, Math.min(view.x.length - 1, Math.round((time - view.x[0]) / step)));
  }
  function inspect(index) {
    const area = plotBounds();
    if (index < 0 || index >= view.x.length || !area) { dismiss(); return; }
    selected = index;
    selectedTime = view.x[index];
    const value = Number.isFinite(view.y[index]) ? view.y[index].toFixed(2) + ' °C' : 'No reading';
    const time = pointClock(selectedTime);
    readout.textContent = time + ' · ' + value;
    tooltip.textContent = time + '\n' + value;
    tooltip.hidden = false;
    frame.setAttribute('aria-describedby', 'history-summary history-tooltip');
    const rect = frame.getBoundingClientRect();
    const x = pointer ? pointer.clientX - rect.left : area.left - rect.left +
      (selectedTime - view.start) * area.width / (view.end - view.start);
    const y = pointer ? pointer.clientY - rect.top : area.top - rect.top +
      (Number.isFinite(view.y[index]) ? (view.high - view.y[index]) / (view.high - view.low) : .5) * area.height;
    const width = tooltip.offsetWidth, height = tooltip.offsetHeight, gap = 12, edge = 8;
    let left = x + gap, top = y - height - gap;
    if (left + width > frame.clientWidth - edge) left = x - width - gap;
    if (top < edge) top = y + gap;
    tooltip.style.left = Math.max(edge, Math.min(frame.clientWidth - width - edge, left)) + 'px';
    tooltip.style.top = Math.max(edge, Math.min(frame.clientHeight - height - edge, top)) + 'px';
  }
  function syncCursor() {
    if (!plot || host.hidden || selected < 0) return;
    try {
      plot.setCursor({left:plot.valToPos(selectedTime,'x'),
        top:Number.isFinite(view.y[selected]) ? plot.valToPos(view.y[selected],'y') : 0});
    } catch (_) { abandonEnhancement(); schedule(); }
  }
  function point(event) {
    const area = plotBounds();
    if (!area || event.clientX < area.left || event.clientX > area.left + area.width ||
        event.clientY < area.top || event.clientY > area.top + area.height) { dismiss(); return; }
    pointer = event.pointerType === 'touch' ? null : {clientX:event.clientX,clientY:event.clientY};
    const time = view.start + (event.clientX - area.left) * (view.end - view.start) / area.width;
    inspect(indexAtTime(time));
    if (!pointer) syncCursor();
  }
  function refreshSelection() {
    // Pointer coordinates survive normal polling; keyboard/touch keep their time.
    if (pointer) point(pointer);
    else if (selectedTime != null) { inspect(indexAtTime(selectedTime)); syncCursor(); }
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
    geometry = {left,right,top,bottom};
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
      ]
    }, [view.x, view.y], host);
  }
  function render() {
    pendingFrame = 0;
    if (document.hidden) return;
    try {
      if (dirty) { view = project(); dirty = false; }
      messages();
      const width = Math.floor(frame.clientWidth), height = Math.floor(frame.clientHeight);
      if (!width || !height) return;
      const color = colors();
      // Always prepare a local frame before attempting optional external code.
      basic(width, height, color);
      if (!libraryReady || libraryFailed || view.x.length < 2 || !view.count) {
        useBasic(); refreshSelection(); return;
      }
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
      refreshSelection();
    } catch (_) {
      // A drawing failure must not escape into the history fetch/error path.
      abandonEnhancement();
      dismiss();
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
    dismiss();
    dirty = true; schedule();
  }));
  frame.addEventListener('keydown', event => {
    if (!['ArrowLeft','ArrowRight','Home','End'].includes(event.key) || !view.x.length) return;
    event.preventDefault();
    const last = view.x.length - 1;
    const next = event.key === 'Home' ? 0 : event.key === 'End' ? last :
      Math.max(0, Math.min(last, (selected < 0 ? last : selected) + (event.key === 'ArrowLeft' ? -1 : 1)));
    pointer = null;
    inspect(next);
    syncCursor();
  });
  // Native pointer selection owns the tooltip for both renderers. uPlot cursor
  // hooks never select another index after our handler (especially at nulls).
  frame.addEventListener('pointermove', event => {
    if (event.pointerType !== 'touch') point(event);
  }, {passive:true});
  frame.addEventListener('pointerdown', event => {
    frame.focus({preventScroll:true});
    point(event);
  }, {passive:true});
  frame.addEventListener('pointerleave', event => { if (event.pointerType !== 'touch') dismiss(); });
  frame.addEventListener('pointercancel', dismiss);
  frame.addEventListener('blur', dismiss);
  document.addEventListener('pointerdown', event => { if (!frame.contains(event.target)) dismiss(); }, {passive:true});
  document.addEventListener('keydown', event => { if (event.key === 'Escape') dismiss(); });
  if (window.ResizeObserver) new ResizeObserver(schedule).observe(frame);
  window.addEventListener('resize', schedule);
  document.addEventListener('room-theme-change', () => {
    try { if (plot) plot.destroy(); } catch (_) {}
    plot = null; host.replaceChildren(); useBasic(); schedule();
  });
  document.addEventListener('visibilitychange', () => { if (!document.hidden) schedule(); });
  window.RoomHistory = {
    update(data, at) {
      if (!data || (cached && (data.session !== cached.session || data.reset))) dismiss();
      cached = data; received = at; stale = false; dirty = true; schedule();
    },
    unavailable() { stale = true; schedule(); }
  };
  schedule();
  if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', loadEnhancement, {once:true});
  else loadEnhancement();
})();
