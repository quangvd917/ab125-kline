#pragma once

// iPhone dashboard served at http://192.168.4.1/. Polls /data about 7 times per second.
static const char DASHBOARD_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="vi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover,user-scalable=no">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="AB125">
<title>AB125</title>
<style>
  :root { --bg:#0b0d10; --card:#161a20; --text:#f2f4f7; --muted:#8a93a0;
          --accent:#3ddc84; --warn:#ffb020; --bad:#ff4d4f; }
  * { box-sizing:border-box; margin:0; padding:0; }
  html,body { height:100%; background:var(--bg); color:var(--text);
              font-family:-apple-system,BlinkMacSystemFont,"SF Pro Display",sans-serif;
              -webkit-user-select:none; user-select:none; }
  body { display:flex; flex-direction:column; gap:14px;
         padding:calc(env(safe-area-inset-top) + 12px) 16px calc(env(safe-area-inset-bottom) + 16px); }
  header { display:flex; justify-content:space-between; align-items:center; }
  header h1 { font-size:15px; font-weight:600; color:var(--muted); letter-spacing:.04em; }
  #status { font-size:13px; padding:4px 10px; border-radius:999px; background:var(--card); }
  #status.ok { color:var(--accent); } #status.warn { color:var(--warn); } #status.bad { color:var(--bad); }
  .speed { text-align:center; padding:8px 0 0; }
  .speed .v { font-size:min(38vw,220px); font-weight:700; line-height:1; font-variant-numeric:tabular-nums; }
  .speed .u { font-size:18px; color:var(--muted); }
  .rpm { background:var(--card); border-radius:16px; padding:14px 16px; }
  .rpm .row { display:flex; justify-content:space-between; align-items:baseline; }
  .rpm .v { font-size:44px; font-weight:700; font-variant-numeric:tabular-nums; }
  .rpm .u { color:var(--muted); font-size:15px; }
  .bar { height:14px; border-radius:7px; background:#232a33; margin-top:10px; overflow:hidden; }
  .bar i { display:block; height:100%; width:0; border-radius:7px; background:var(--accent);
           transition:width .12s linear, background .2s; }
  .grid { display:grid; grid-template-columns:1fr 1fr; gap:12px; }
  .tile { background:var(--card); border-radius:16px; padding:12px 14px; }
  .tile .l { font-size:13px; color:var(--muted); }
  .tile .v { font-size:30px; font-weight:650; margin-top:2px; font-variant-numeric:tabular-nums; }
  .tile .v small { font-size:15px; color:var(--muted); font-weight:500; }
  .warn-t { color:var(--warn); } .bad-t { color:var(--bad); }
  .stale .speed, .stale .rpm, .stale .grid { opacity:.35; }
  @media (orientation:landscape) {
    body { display:grid; grid-template-columns:1.1fr 1fr; grid-template-rows:auto 1fr auto; }
    header { grid-column:1 / 3; }
    .speed { grid-row:2 / 4; align-self:center; }
  }
</style>
</head>
<body class="stale">
<header><h1>AIR BLADE 125</h1><span id="status">Đang kết nối…</span></header>
<section class="speed"><div class="v" id="kmh">--</div><div class="u">km/h</div></section>
<section class="rpm">
  <div class="row"><span class="v" id="rpm">--</span><span class="u">vòng/phút</span></div>
  <div class="bar"><i id="rpmbar"></i></div>
</section>
<section class="grid">
  <div class="tile"><div class="l">Nhiệt độ máy</div><div class="v" id="ect">--</div></div>
  <div class="tile"><div class="l">Ắc quy</div><div class="v" id="batt">--</div></div>
  <div class="tile"><div class="l">Bướm ga</div><div class="v" id="tps">--</div></div>
  <div class="tile"><div class="l">Khí nạp</div><div class="v" id="iat">--</div></div>
</section>
<script>
const RPM_MAX = 10000;
const $ = id => document.getElementById(id);
const STATUS = { connecting:"Đang kết nối ECU…", "no-echo":"Không có tín hiệu K-Line",
                 "init-fail":"ECU không phản hồi", lost:"Mất kết nối, thử lại…", starting:"Đang khởi động…" };

function setStatus(text, cls) { const s = $("status"); s.textContent = text; s.className = cls; }

function render(d) {
  const fresh = d.valid && d.age < 1500;
  document.body.classList.toggle("stale", !fresh);
  if (!fresh) { setStatus(STATUS[d.status] || d.status, "warn"); return; }
  setStatus("Đang nhận dữ liệu", "ok");

  $("kmh").textContent = d.kmh;
  $("rpm").textContent = d.rpm.toLocaleString("vi-VN");
  const pct = Math.min(100, d.rpm / RPM_MAX * 100);
  const bar = $("rpmbar");
  bar.style.width = pct + "%";
  bar.style.background = d.rpm > 8000 ? "var(--bad)" : d.rpm > 6500 ? "var(--warn)" : "var(--accent)";

  const ect = $("ect");
  ect.innerHTML = d.ect + "<small>°C</small>";
  ect.className = "v" + (d.ect > 105 ? " bad-t" : d.ect > 98 ? " warn-t" : "");

  const batt = $("batt");
  batt.innerHTML = d.batt.toFixed(1) + "<small>V</small>";
  const lowCharge = d.rpm > 1000 && d.batt < 13.0;
  batt.className = "v" + (d.batt > 15.0 || d.batt < 11.8 ? " bad-t" : lowCharge ? " warn-t" : "");

  $("tps").textContent = d.tps;
  $("iat").innerHTML = d.iat + "<small>°C</small>";
}

async function poll() {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), 1000);
  try {
    const r = await fetch("/data", { cache:"no-store", signal:ctrl.signal });
    render(await r.json());
  } catch (e) {
    document.body.classList.add("stale");
    setStatus("Mất WiFi tới ESP32", "bad");
  } finally {
    clearTimeout(timer);
    setTimeout(poll, 140);
  }
}
poll();
</script>
</body>
</html>
)HTML";
