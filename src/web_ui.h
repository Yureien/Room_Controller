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
  <title>Room Controller</title>
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
    .section-title { margin:0 0 14px; font-size:18px; }
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
    .lamp { margin-top:18px; border:1px solid #d7e1e5; border-radius:24px;
      padding:22px; background:var(--paper); }
    .lamp h2 { margin:0; font-size:18px; }
    .lamp-head { display:flex; align-items:center; justify-content:space-between; gap:12px; }
    #lamp-link { font-size:11px; color:var(--muted); }
    #lamp-name { margin:6px 0 15px; font-size:12px; color:var(--muted); }
    .lamp-power { display:flex; gap:8px; margin-bottom:14px; }
    .lamp button { border-radius:10px; padding:9px 13px; font-size:12px; }
    .lamp button.active { background:#eaf6f6; border-color:#89bcc3; }
    .palette { display:grid; grid-template-columns:repeat(5,1fr); gap:8px; }
    .lamp .swatch { padding:0; height:39px; border:2px solid #fff;
      box-shadow:0 0 0 1px var(--line); }
    .swatch.selected { outline:2px solid var(--ink); outline-offset:2px; }
    .lamp-row { display:flex; align-items:center; gap:10px; margin-top:16px; }
    .lamp-row label { font-size:12px; min-width:65px; }
    .lamp-row input[type=range], .lamp-row select { min-width:0; flex:1; }
    .lamp-row input[type=color] { height:36px; width:70px; padding:2px;
      border:1px solid var(--line); border-radius:8px; background:white; }
    .lamp-row output { font-size:12px; min-width:34px; }
    .lamp-row select { padding:8px 4px; border:1px solid var(--line); border-radius:8px; }
    #lamp-feedback { margin:15px 0 0; font-size:12px; line-height:1.5; color:var(--muted); }
    #lamp-feedback.error { color:#b23c3c; }
    .lamp-note { font-size:10px; color:var(--muted); line-height:1.5; margin:10px 0 0; }
    .history { margin-top:18px; padding:20px; border:1px solid #d7e1e5;
      border-radius:24px; background:var(--paper); }
    .history h2 { margin:0 0 12px; font-size:18px; }
    #temperature-chart { display:block; width:100%; height:180px; }
    #history-status { font-size:11px; color:var(--muted); line-height:1.5; }
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
      <div><p class="eyebrow">AC · Temperature · Lights</p><h1>Room Controller</h1></div>
      <div id="connection" class="connection" role="status">
        <span class="dot"></span><span id="connection-text">Connecting</span>
      </div>
    </header>
    <section class="remote" aria-label="Air conditioner remote">
      <h2 class="section-title">Blue Star AC</h2>
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
    <section class="history" aria-label="Temperature history">
      <h2>Room temperature</h2>
      <canvas id="temperature-chart" role="img" aria-label="Temperature history in degrees Celsius">
        Temperature graph requires a browser with canvas support.
      </canvas>
      <p id="history-status" role="status">Loading temperature history…</p>
      <p class="lamp-note">One sample every 5 seconds · Last 24 hours · History resets on restart.</p>
    </section>
    <section class="lamp" aria-label="LED light strip">
      <div class="lamp-head"><h2>LED strip</h2><span id="lamp-link" role="status">Not set up</span></div>
      <p id="lamp-name">Select your strip over serial.</p>
      <div class="lamp-power">
        <button id="lamp-on" data-lamp-command="on" aria-pressed="false">On</button>
        <button id="lamp-off" data-lamp-command="off" aria-pressed="false">Off</button>
        <button id="lamp-connect" data-lamp-command="reconnect">Connect</button>
        <button id="lamp-disconnect" data-lamp-command="disconnect">Disconnect</button>
      </div>
      <div class="palette" aria-label="Preset colors">
        <button class="swatch" style="background:#ff0000" data-lamp-command="color #ff0000" title="Red" aria-label="Red" aria-pressed="false"></button>
        <button class="swatch" style="background:#ffa500" data-lamp-command="color #ffa500" title="Orange" aria-label="Orange" aria-pressed="false"></button>
        <button class="swatch" style="background:#ffff00" data-lamp-command="color #ffff00" title="Yellow" aria-label="Yellow" aria-pressed="false"></button>
        <button class="swatch" style="background:#00ff00" data-lamp-command="color #00ff00" title="Green" aria-label="Green" aria-pressed="false"></button>
        <button class="swatch" style="background:#00ffff" data-lamp-command="color #00ffff" title="Cyan" aria-label="Cyan" aria-pressed="false"></button>
        <button class="swatch" style="background:#0000ff" data-lamp-command="color #0000ff" title="Blue" aria-label="Blue" aria-pressed="false"></button>
        <button class="swatch" style="background:#800080" data-lamp-command="color #800080" title="Purple" aria-label="Purple" aria-pressed="false"></button>
        <button class="swatch" style="background:#ff00ff" data-lamp-command="color #ff00ff" title="Magenta" aria-label="Magenta" aria-pressed="false"></button>
        <button class="swatch" style="background:#ffc0cb" data-lamp-command="color #ffc0cb" title="Pink" aria-label="Pink" aria-pressed="false"></button>
        <button class="swatch" style="background:#ffffff" data-lamp-command="color #ffffff" title="White" aria-label="White" aria-pressed="false"></button>
      </div>
      <div class="lamp-row">
        <label for="lamp-color">Color</label><input id="lamp-color" type="color" value="#ffffff">
        <button id="lamp-color-apply">Apply color</button>
      </div>
      <div class="lamp-row">
        <label for="lamp-brightness">Brightness</label>
        <input id="lamp-brightness" type="range" min="0" max="100" value="100">
        <output id="lamp-brightness-value" for="lamp-brightness">100%</output>
        <button id="lamp-brightness-apply">Apply</button>
      </div>
      <div class="lamp-row">
        <label for="lamp-effect">Effect</label><select id="lamp-effect">
<option value="-1">Solid color</option>
          <optgroup label="Basic">
            <option value="0">0: Auto Play</option>
            <option value="1">1: Magic Forward</option>
            <option value="2">2: Magic Back</option>
            <option value="212">212: 7-Color Energy</option>
            <option value="193">193: 7-Color Jump</option>
            <option value="194">194: R-G-B Jump</option>
            <option value="195">195: Y-C-P Jump</option>
            <option value="196">196: 7-Color Strobe</option>
            <option value="197">197: R-G-B Strobe</option>
            <option value="198">198: Y-C-P Strobe</option>
            <option value="199">199: 7-Color Gradual</option>
            <option value="200">200: R-Y Gradual</option>
            <option value="201">201: R-P Gradual</option>
            <option value="202">202: G-C Gradual</option>
            <option value="203">203: G-Y Gradual</option>
            <option value="204">204: B-P Gradual</option>
            <option value="205">205: Red Marquee</option>
            <option value="206">206: Green Marquee</option>
            <option value="207">207: Blue Marquee</option>
            <option value="208">208: Yellow Marquee</option>
            <option value="209">209: Cyan Marquee</option>
            <option value="210">210: Purple Marquee</option>
            <option value="211">211: White Marquee</option>
            <option value="77">77: 7-Color Race</option>
            <option value="78">78: 7-Color Race Back</option>
            <option value="79">79: R-G-B Race</option>
            <option value="80">80: R-G-B Race Back</option>
            <option value="81">81: Y-C-P Race</option>
            <option value="82">82: Y-C-P Race Back</option>
            <option value="83">83: 7-Color Wave</option>
            <option value="84">84: 7-Color Wave Back</option>
            <option value="85">85: R-G-B Wave</option>
            <option value="86">86: R-G-B Wave Back</option>
            <option value="87">87: Y-C-P Wave</option>
            <option value="88">88: Y-C-P Wave Back</option>
            <option value="181">181: 7-Color Flush</option>
            <option value="182">182: 7-Color Flush Back</option>
            <option value="183">183: R-G-B Flush</option>
            <option value="184">184: R-G-B Flush Back</option>
            <option value="185">185: Y-C-P Flush</option>
            <option value="186">186: Y-C-P Flush Back</option>
            <option value="187">187: 7-Color Flush Close</option>
            <option value="188">188: 7-Color Flush Open</option>
            <option value="189">189: R-G-B Flush Close</option>
            <option value="190">190: R-G-B Flush Open</option>
            <option value="191">191: Y-C-P Flush Close</option>
            <option value="192">192: Y-C-P Flush Open</option>
          </optgroup>
          <optgroup label="Curtain">
            <option value="57">57: 7-Color Close</option>
            <option value="58">58: 7-Color Open</option>
            <option value="59">59: R-G-B Close</option>
            <option value="60">60: R-G-B Open</option>
            <option value="61">61: Y-C-P Close</option>
            <option value="62">62: Y-C-P Open</option>
            <option value="63">63: Red Close</option>
            <option value="64">64: Red Open</option>
            <option value="65">65: Green Close</option>
            <option value="66">66: Green Open</option>
            <option value="67">67: Blue Close</option>
            <option value="68">68: Blue Open</option>
            <option value="69">69: Yellow Close</option>
            <option value="70">70: Yellow Open</option>
            <option value="71">71: Cyan Close</option>
            <option value="72">72: Cyan Open</option>
            <option value="73">73: Purple Close</option>
            <option value="74">74: Purple Open</option>
            <option value="75">75: White Close</option>
            <option value="76">76: White Open</option>
          </optgroup>
          <optgroup label="Trans">
            <option value="3">3: 7-Color Trans</option>
            <option value="4">4: 7-Color Trans Back</option>
            <option value="5">5: R-G-B Trans</option>
            <option value="6">6: R-G-B Trans Back</option>
            <option value="7">7: Y-C-P Trans</option>
            <option value="8">8: Y-C-P Trans Back</option>
            <option value="9">9: 6-Color to Red</option>
            <option value="10">10: 6-Color to Red Back</option>
            <option value="11">11: 6-Color to Green</option>
            <option value="12">12: 6-Color to Green Back</option>
            <option value="13">13: 6-Color to Blue</option>
            <option value="14">14: 6-Color to Blue Back</option>
            <option value="15">15: 6-Color to Cyan</option>
            <option value="16">16: 6-Color to Cyan Back</option>
            <option value="17">17: 6-Color to Yellow</option>
            <option value="18">18: 6-Color to Yellow Back</option>
            <option value="19">19: 6-Color to Purple</option>
            <option value="20">20: 6-Color to Purple Back</option>
            <option value="21">21: 6-Color to White</option>
            <option value="22">22: 6-Color to White Back</option>
          </optgroup>
          <optgroup label="Water">
            <option value="39">39: 7-Color Water</option>
            <option value="40">40: 7-Color Water Back</option>
            <option value="41">41: R-G-B Water</option>
            <option value="42">42: R-G-B Water Back</option>
            <option value="43">43: Y-C-P Water</option>
            <option value="44">44: Y-C-P Water Back</option>
            <option value="45">45: R-G Water</option>
            <option value="46">46: R-G Water Back</option>
            <option value="47">47: G-B Water</option>
            <option value="48">48: G-B Water Back</option>
            <option value="49">49: Y-B Water</option>
            <option value="50">50: Y-B Water Back</option>
            <option value="51">51: Y-C Water</option>
            <option value="52">52: Y-C Water Back</option>
            <option value="53">53: C-P Water</option>
            <option value="54">54: C-P Water Back</option>
            <option value="55">55: White Water</option>
            <option value="56">56: White Water Back</option>
          </optgroup>
          <optgroup label="Flow">
            <option value="143">143: W-R-W Flow</option>
            <option value="144">144: W-R-W Flow Back</option>
            <option value="145">145: W-G-W Flow</option>
            <option value="146">146: W-G-W Flow Back</option>
            <option value="147">147: W-B-W Flow</option>
            <option value="148">148: W-B-W Flow Back</option>
            <option value="149">149: W-Y-W Flow</option>
            <option value="150">150: W-Y-W Flow Back</option>
            <option value="151">151: W-C-W Flow</option>
            <option value="152">152: W-C-W Flow Back</option>
            <option value="153">153: W-P-W Flow</option>
            <option value="154">154: W-P-W Flow Back</option>
            <option value="155">155: R-W-R Flow</option>
            <option value="156">156: R-W-R Flow Back</option>
            <option value="157">157: G-W-G Flow</option>
            <option value="158">158: G-W-G Flow Back</option>
            <option value="159">159: B-W-B Flow</option>
            <option value="160">160: B-W-B Flow Back</option>
            <option value="161">161: Y-W-Y Flow</option>
            <option value="162">162: Y-W-Y Flow Back</option>
            <option value="163">163: C-W-C Flow</option>
            <option value="164">164: C-W-C Flow Back</option>
            <option value="165">165: P-W-P Flow</option>
            <option value="166">166: P-W-P Flow Back</option>
          </optgroup>
          <optgroup label="Tail">
            <option value="23">23: 7-Color Tail</option>
            <option value="24">24: 7-Color Tail Back</option>
            <option value="25">25: Red Tail</option>
            <option value="26">26: Red Tail Back</option>
            <option value="27">27: Green Tail</option>
            <option value="28">28: Green Tail Back</option>
            <option value="29">29: Blue Tail</option>
            <option value="30">30: Blue Tail Back</option>
            <option value="31">31: Yellow Tail</option>
            <option value="32">32: Yellow Tail Back</option>
            <option value="33">33: Cyan Tail</option>
            <option value="34">34: Cyan Tail Back</option>
            <option value="35">35: Purple Tail</option>
            <option value="36">36: Purple Tail Back</option>
            <option value="37">37: White Tail</option>
            <option value="38">38: White Tail Back</option>
          </optgroup>
          <optgroup label="Run">
            <option value="89">89: Red Running</option>
            <option value="91">91: Green Running</option>
            <option value="93">93: Blue Running</option>
            <option value="95">95: Yellow Running</option>
            <option value="97">97: Cyan Running</option>
            <option value="99">99: Purple Running</option>
            <option value="101">101: White Running</option>
            <option value="103">103: 7-Color Running</option>
            <option value="105">105: R-G-B Running</option>
            <option value="107">107: Y-C-P Running</option>
            <option value="109">109: B-P-C-Y Running</option>
            <option value="111">111: B-G-C-Y Running</option>
            <option value="113">113: Red-Dot in White Running</option>
            <option value="115">115: Green-Dot in Red Running</option>
            <option value="117">117: Blue-Dot in Green Running</option>
            <option value="119">119: Yellow-Dot in Blue Running</option>
            <option value="121">121: Cyan-Dot in Yellow Running</option>
            <option value="123">123: Purple-Dot in Cyan Running</option>
            <option value="125">125: White-Dot in Purple Running</option>
            <option value="127">127: White-Dot in Red Running</option>
            <option value="129">129: 7-Color in Red Running</option>
            <option value="131">131: 7-Color in Green Running</option>
            <option value="133">133: 7-Color in Blue Running</option>
            <option value="135">135: 7-Color in Yellow Running</option>
            <option value="137">137: 7-Color in Cyan Running</option>
            <option value="139">139: 7-Color in Purple Running</option>
            <option value="141">141: 7-Color in White Running</option>
            <option value="167">167: Green-Dot in Blue Running</option>
            <option value="169">169: Green-Dot in Red Running</option>
            <option value="171">171: Red-Dot in Blue Running</option>
            <option value="173">173: Cyan-Dot in Yellow Running</option>
            <option value="175">175: Yellow-Dot in Purple Running</option>
            <option value="177">177: White-Dot in Yellow Running</option>
            <option value="179">179: Yellow-Dot in White Running</option>
          </optgroup>
          <optgroup label="Runback">
            <option value="90">90: Red Run Back</option>
            <option value="92">92: Green Run Back</option>
            <option value="94">94: Blue Run Back</option>
            <option value="96">96: Yellow Run Back</option>
            <option value="98">98: Cyan Run Back</option>
            <option value="100">100: Purple Run Back</option>
            <option value="102">102: White Run Back</option>
            <option value="104">104: 7-Color Run Back</option>
            <option value="106">106: R-G-B Run Back</option>
            <option value="108">108: Y-C-P Run Back</option>
            <option value="110">110: B-P-C-Y Run Back</option>
            <option value="112">112: B-G-C-Y Run Back</option>
            <option value="114">114: Red-Dot in White Run Back</option>
            <option value="116">116: Green-Dot in Red Run Back</option>
            <option value="118">118: Blue-Dot in Green Run Back</option>
            <option value="120">120: Yellow-Dot in Blue Run Back</option>
            <option value="122">122: Cyan-Dot in Yellow Run Back</option>
            <option value="124">124: Purple-Dot in Cyan Run Back</option>
            <option value="126">126: White-Dot in Purple Run Back</option>
            <option value="128">128: White-Dot in Red Run Back</option>
            <option value="130">130: 7-Color in Red Run Back</option>
            <option value="132">132: 7-Color in Green Run Back</option>
            <option value="134">134: 7-Color in Blue Run Back</option>
            <option value="136">136: 7-Color in Yellow Run Back</option>
            <option value="138">138: 7-Color in Cyan Run Back</option>
            <option value="140">140: 7-Color in Purple Run Back</option>
            <option value="142">142: 7-Color in White Run Back</option>
            <option value="168">168: Green-Dot in Blue Run Back</option>
            <option value="170">170: Green-Dot in Red Run Back</option>
            <option value="172">172: Red-Dot in Blue Run Back</option>
            <option value="174">174: Cyan-Dot in Yellow Run Back</option>
            <option value="176">176: Yellow-Dot in Purple Run Back</option>
            <option value="178">178: Yellow-Dot in Purple Run Back</option>
            <option value="180">180: White-Dot in Yellow Run Back</option>
          </optgroup>
        </select><button id="lamp-effect-apply">Apply</button>
      </div>
      <div class="lamp-row">
        <label for="lamp-speed">Speed</label>
        <input id="lamp-speed" type="range" min="0" max="100" value="50">
        <output id="lamp-speed-value" for="lamp-speed">50%</output>
        <button id="lamp-speed-apply">Apply</button>
      </div>
      <p id="lamp-feedback" role="status" aria-live="polite">Set up over serial: lamp scan, then lamp pair &lt;number&gt;.</p>
      <p class="lamp-note">Connects when you send a setting; Bluetooth sleeps after 30s idle.
        Strip settings stay active. Controls show the last requested settings.</p>
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
    let historyData = null;
    let historyReceived = 0;
    let historyLoading = false;

    function drawHistory() {
      const canvas = $('temperature-chart');
      const width = canvas.clientWidth;
      const height = 180;
      if (!width) return;
      const scale = window.devicePixelRatio || 1;
      canvas.width = Math.round(width * scale);
      canvas.height = Math.round(height * scale);
      const ctx = canvas.getContext('2d');
      if (!ctx) return;
      ctx.scale(scale, scale);
      ctx.font = '11px system-ui';
      ctx.fillStyle = '#677b83';
      const data = historyData && historyData.samples;
      const valid = data ? data.filter(Number.isFinite) : [];
      if (!valid.length) {
        ctx.fillText('Waiting for measured temperatures', 12, 85);
        return;
      }
      const low = Math.floor(Math.min(...valid) - 0.5);
      const high = Math.ceil(Math.max(...valid) + 0.5);
      const left = 47, top = 12, right = width - 8, bottom = height - 30;
      const x = i => left + i * (right - left) / Math.max(1, data.length - 1);
      const y = v => bottom - (v - low) * (bottom - top) / (high - low);
      for (let i = 0; i <= 2; i++) {
        const v = low + (high - low) * i / 2;
        ctx.strokeStyle = '#dce5e8';
        ctx.beginPath(); ctx.moveTo(left, y(v)); ctx.lineTo(right, y(v)); ctx.stroke();
        ctx.fillText(v.toFixed(1) + '°C', 0, y(v) + 4);
      }
      ctx.strokeStyle = '#087d8c'; ctx.lineWidth = 2;
      ctx.beginPath();
      let joined = false;
      data.forEach((v, i) => {
        if (!Number.isFinite(v)) { joined = false; return; }
        if (joined) ctx.lineTo(x(i), y(v)); else ctx.moveTo(x(i), y(v));
        joined = true;
      });
      ctx.stroke();
      // Mark individual samples too, so a lone reading or isolated point shows.
      ctx.fillStyle = '#087d8c';
      data.forEach((v, i) => {
        if (!Number.isFinite(v)) return;
        ctx.beginPath(); ctx.arc(x(i), y(v), 1.5, 0, Math.PI * 2); ctx.fill();
      });
      ctx.fillStyle = '#677b83';
      const newest = historyReceived - historyData.age * 1000;
      const label = time => new Date(time).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});
      ctx.fillText(label(newest - (data.length - 1) * historyData.interval * 1000), left, height - 8);
      ctx.textAlign = 'right'; ctx.fillText(label(newest), right, height - 8);
      canvas.setAttribute('aria-label', valid.length + ' temperature readings, ' +
        Math.min(...valid).toFixed(2) + ' to ' + Math.max(...valid).toFixed(2) + ' degrees Celsius');
    }

    async function requestHistory() {
      if (historyLoading || document.hidden) return;
      historyLoading = true;
      const abort = new AbortController();
      const timeout = setTimeout(() => abort.abort(), 10000);
      try {
        const response = await fetch('/api/temperature/history', {cache:'no-store',signal:abort.signal});
        const next = await response.json();
        if (!response.ok || !Array.isArray(next.samples) || next.samples.length > 17280 ||
            next.interval !== 5 || !Number.isFinite(next.age) ||
            !next.samples.every(v => v === null || Number.isFinite(v))) throw new Error('Invalid history');
        historyData = next;
        historyReceived = Date.now();
        const valid = next.samples.filter(Number.isFinite);
        $('history-status').textContent = valid.length ? valid.length + ' readings · Min ' +
          Math.min(...valid).toFixed(2) + '°C · Max ' + Math.max(...valid).toFixed(2) +
          '°C · Gaps indicate missing readings.' : 'No readings yet. Check the sensor.';
        drawHistory();
      } catch (error) {
        $('history-status').textContent = 'History unavailable. Retrying when the controller is reachable.';
      } finally {
        clearTimeout(timeout);
        historyLoading = false;
      }
    }
    let state = null;
    let busy = false;
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
        lampEdited.delete(lampPendingEdit.field);
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
      lampLocalError = '';
      updateLampControls();
      const abort = new AbortController();
      const timeout = setTimeout(() => abort.abort(), 5000);
      try {
        const response = await fetch('/api/lamp/command', {
          method:'POST',cache:'no-store',signal:abort.signal,
          headers:{'Content-Type':'application/x-www-form-urlencoded'},
          body:new URLSearchParams({command}).toString()
        });
        const next = await response.json();
        if (!next.lamp || typeof next.power !== 'boolean') throw new Error('Invalid controller response');
        if (!response.ok || !next.ok) lampLocalError = next.message || 'Lamp command rejected.';
        else if (field) lampPendingEdit = {field, completed:next.lamp.completed};
        render(next);
      } catch (error) {
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
        if (state && command.startsWith('temp ')) {
          disabled ||= state.mode === 1;
          disabled ||= command === 'temp up' && state.temperature >= 30;
          disabled ||= command === 'temp down' && state.temperature <= 16;
        }
        button.disabled = disabled;
      }
      updateLampControls();
    }

    function render(next) {
      state = next;
      renderLamp();
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
        busy = false;
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
    setInterval(requestHistory, 5000);
    document.addEventListener('visibilitychange', () => {
      if (document.hidden) clearTimeout(lampPollTimer);
      else { request(); requestHistory(); }
    });
    window.addEventListener('resize', drawHistory);
  </script>
</body>
</html>
)HTML";

#endif  // WEB_UI_H_
