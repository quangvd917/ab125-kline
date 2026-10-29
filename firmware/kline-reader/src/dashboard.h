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
  #dtcChip { display:flex; justify-content:space-between; align-items:center; width:100%;
             background:var(--card); color:var(--text); border:0; border-radius:16px;
             padding:14px 16px; font:inherit; font-size:16px; text-align:left; cursor:pointer; }
  #dtcChip .s { font-weight:600; } #dtcChip .go { color:var(--muted); font-size:20px; }
  .ok-t { color:var(--accent); }
  @media (orientation:landscape) {
    body { display:grid; grid-template-columns:1.1fr 1fr; grid-template-rows:auto 1fr auto auto; }
    header { grid-column:1 / 3; }
    .speed { grid-row:2 / 5; align-self:center; }
  }

  /* Fault code view (opened with #dtc so the back gesture closes it) */
  #dtcView { position:fixed; inset:0; background:var(--bg); overflow-y:auto; display:none;
             padding:calc(env(safe-area-inset-top) + 12px) 16px calc(env(safe-area-inset-bottom) + 24px); }
  body.show-dtc #dtcView { display:block; }
  .vhead { display:flex; align-items:center; gap:10px; margin-bottom:14px; }
  .vhead h2 { flex:1; font-size:20px; }
  .btn { background:var(--card); color:var(--text); border:0; border-radius:999px; padding:8px 14px;
         font:inherit; font-size:14px; cursor:pointer; }
  .btn.on { background:var(--warn); color:#1a1300; font-weight:600; }
  .meta { color:var(--muted); font-size:13px; margin-bottom:14px; line-height:1.4; }
  .demo-banner { display:none; background:#3a2a00; color:var(--warn); border-radius:12px;
                 padding:10px 12px; font-size:14px; margin-bottom:14px; }
  body.dtc-demo .demo-banner { display:block; }
  .sec { font-size:13px; font-weight:600; letter-spacing:.06em; color:var(--muted); margin:18px 0 8px; }
  .code { background:var(--card); border-radius:14px; padding:12px 14px; margin-bottom:10px;
          display:grid; grid-template-columns:auto 1fr; gap:2px 14px; align-items:center; }
  .code .num { grid-row:1 / 3; font-size:30px; font-weight:700; font-variant-numeric:tabular-nums; min-width:76px; }
  .code.cur .num { color:var(--bad); } .code.past .num { color:var(--warn); }
  .code .d { font-size:15px; } .code .b { font-size:13px; color:var(--muted); }
  .empty { background:var(--card); border-radius:14px; padding:14px; color:var(--muted); font-size:15px; }
  #clearBox { display:none; margin-top:20px; }
  #clearBtn { width:100%; padding:13px; border-radius:12px; background:#3a1416; color:var(--bad);
              font-weight:600; font-size:16px; }
  #clearBtn:disabled { opacity:.4; cursor:default; }
  #clearMsg { margin:8px 0 0; }
  details { margin-top:22px; color:var(--muted); font-size:13px; }
  details pre { white-space:pre-wrap; word-break:break-all; margin-top:8px; font-size:12px;
                background:var(--card); border-radius:10px; padding:10px; -webkit-user-select:text; user-select:text; }
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
<button id="dtcChip" onclick="location.hash='dtc'">
  <span>Mã lỗi: <span class="s" id="dtcSummary">đang đọc…</span></span><span class="go">›</span>
</button>

<div id="dtcView">
  <div class="vhead">
    <button class="btn" onclick="history.back()">‹ Quay lại</button>
    <h2>Mã lỗi</h2>
    <button class="btn" id="demoBtn" onclick="toggleDemo()">Dữ liệu mẫu</button>
  </div>
  <div class="demo-banner">DEMO: đây là dữ liệu mẫu, không phải lỗi thật của xe.</div>
  <div class="meta" id="dtcMeta">Đang đọc mã lỗi từ ECU…</div>
  <div class="sec">ĐANG XẢY RA</div><div id="dtcCurrent"></div>
  <div class="sec">ĐÃ LƯU TRONG ECU</div><div id="dtcStored"></div>
  <div id="clearBox">
    <button class="btn" id="clearBtn" onclick="clearCodes()">Xóa lỗi</button>
    <div class="meta" id="clearMsg"></div>
  </div>
  <div class="meta" style="margin-top:18px">Đèn FI chỉ nháy được mã chính: 1 lần nháy dài = 10,
    1 lần nháy ngắn = 1. Ý nghĩa mã theo bảng chung của Honda PGM-FI.</div>
  <details><summary>Dữ liệu thô từ ECU</summary><pre id="dtcRaw">--</pre></details>
</div>
<script>
const RPM_MAX = 10000;
let lastLive = { at: 0, rpm: 0 };  // last fresh live reading, used to allow clearing codes
const $ = id => document.getElementById(id);
const STATUS = { connecting:"Đang kết nối ECU…", "no-echo":"Không có tín hiệu K-Line",
                 "init-fail":"ECU không phản hồi", lost:"Mất kết nối, thử lại…", starting:"Đang khởi động…" };

function setStatus(text, cls) { const s = $("status"); s.textContent = text; s.className = cls; }

function render(d) {
  const fresh = d.valid && d.age < 1500;
  document.body.classList.toggle("stale", !fresh);
  if (!fresh) { setStatus(STATUS[d.status] || d.status, "warn"); return; }
  setStatus("Đang nhận dữ liệu", "ok");
  lastLive = { at: Date.now(), rpm: d.rpm };

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

// ---------------- Fault codes ----------------
const DTC_TEXT = {
  "01-01":"Cảm biến áp suất cổ hút (MAP): điện áp thấp",
  "01-02":"Cảm biến áp suất cổ hút (MAP): điện áp cao",
  "02-01":"Cảm biến MAP hoạt động bất thường (kiểm tra ống chân không)",
  "07-01":"Cảm biến nhiệt độ nước làm mát: điện áp thấp",
  "07-02":"Cảm biến nhiệt độ nước làm mát: điện áp cao",
  "08-01":"Cảm biến vị trí bướm ga: điện áp thấp",
  "08-02":"Cảm biến vị trí bướm ga: điện áp cao",
  "09-01":"Cảm biến nhiệt độ khí nạp: điện áp thấp",
  "09-02":"Cảm biến nhiệt độ khí nạp: điện áp cao",
  "11-01":"Cảm biến tốc độ: mất tín hiệu",
  "12-01":"Kim phun: lỗi mạch",
  "18-01":"Cảm biến trục cam: mất tín hiệu",
  "19-01":"Cảm biến trục khuỷu: mất tín hiệu",
  "21-01":"Cảm biến oxy: hoạt động bất thường",
  "23-01":"Bộ sấy cảm biến oxy: lỗi",
  "29-01":"Van điều khiển gió không tải (IACV): lỗi mạch",
  "33-02":"Bộ nhớ EEPROM của ECU: lỗi",
  "54-01":"Cảm biến nghiêng đổ xe: điện áp thấp",
  "54-02":"Cảm biến nghiêng đổ xe: điện áp cao",
  "86-01":"Lỗi truyền thông nối tiếp"
};
const DTC_DEMO = { valid:true, age:4000, error:"", current:["08-01"], stored:["07-01","54-01"],
                   raw:["(dữ liệu mẫu)"] };
let dtcData = null, demo = false;

function blinks(main) {
  const long = Math.floor(main / 10), short = main % 10;
  if (!long) return `Đèn FI nháy ${short} lần`;
  return `Đèn FI nháy ${long} lần dài` + (short ? `, ${short} lần ngắn` : "");
}

function codeHtml(code, cls) {
  const [m, s] = code.split("-").map(n => parseInt(n, 10));
  const text = DTC_TEXT[code] || "Chưa rõ ý nghĩa";
  return `<div class="code ${cls}"><div class="num">${m}-${s}</div>` +
         `<div class="d">${text}</div><div class="b">${blinks(m)}</div></div>`;
}

function ago(ms) {
  const s = Math.round(ms / 1000);
  return s < 60 ? `${s} giây trước` : `${Math.round(s / 60)} phút trước`;
}

function renderDtc() { renderDtcCodes(); renderClear(); }

function renderDtcCodes() {
  const d = demo ? DTC_DEMO : dtcData;
  document.body.classList.toggle("dtc-demo", demo);
  $("demoBtn").classList.toggle("on", demo);
  const sum = $("dtcSummary");
  if (!d || !d.valid) {
    sum.textContent = d && d.error ? "không đọc được" : "đang đọc…";
    sum.className = "s warn-t";
    $("dtcMeta").textContent = d && d.error ? `ECU không trả lời yêu cầu đọc mã lỗi (${d.error}).`
                                            : "Đang đọc mã lỗi từ ECU…";
    $("dtcCurrent").innerHTML = $("dtcStored").innerHTML = "";
    $("dtcRaw").textContent = d && d.raw && d.raw.length ? d.raw.join("\n") : "--";
    return;
  }
  const nCur = d.current.length, nOld = d.stored.length;
  if (nCur) { sum.textContent = `⚠ ${nCur} lỗi đang xảy ra`; sum.className = "s bad-t"; }
  else if (nOld) { sum.textContent = `${nOld} lỗi đã lưu`; sum.className = "s warn-t"; }
  else { sum.textContent = "không có lỗi"; sum.className = "s ok-t"; }
  if (demo) sum.textContent += " (DEMO)";

  $("dtcMeta").textContent = `Cập nhật ${ago(d.age)}. ECU được đọc lại mỗi 30 giây.`;
  $("dtcCurrent").innerHTML = nCur ? d.current.map(c => codeHtml(c, "cur")).join("")
                                   : `<div class="empty">Không có lỗi</div>`;
  $("dtcStored").innerHTML = nOld ? d.stored.map(c => codeHtml(c, "past")).join("")
                                  : `<div class="empty">Không có lỗi</div>`;
  $("dtcRaw").textContent = d.raw.length ? d.raw.join("\n") : "--";
}

function toggleDemo() { demo = !demo; renderDtc(); }

// ---------------- Clearing codes ----------------
const CLEAR_MSG = {
  "engine-running": "ECU từ chối vì máy đang nổ. Tắt máy, để khóa ở ON rồi thử lại.",
  "no-ecu": "Chưa kết nối được ECU.",
  "no-reply": "ECU không trả lời lệnh xóa. Có thể xe này không hỗ trợ; dùng cách chập chân SCS.",
  "timeout": "Mất kết nối ECU trước khi gửi được lệnh. Thử lại.",
  "busy": "Đang xóa, chờ chút…",
  "disabled": "Firmware này tắt tính năng xóa lỗi.",
  "still-present": "ECU đã nhận lệnh nhưng mã lỗi vẫn còn. Nếu lỗi đang xảy ra (ví dụ giắc chưa cắm chặt), ECU sẽ ghi lại ngay.",
  "verify-failed": "Đã gửi lệnh nhưng không đọc lại được mã lỗi để kiểm tra. Xem lại sau vài giây."
};
let clearUntil = 0;     // poll /dtc quickly until this time after a clear request
let clearLocalMsg = ""; // message from the POST reply, shown until /dtc reports a result

function renderClear() {
  const d = dtcData, box = $("clearBox"), btn = $("clearBtn"), msg = $("clearMsg");
  const c = d && d.clear ? d.clear : { state:"idle", msg:"", age:0 };
  const hasCodes = d && d.valid && (d.current.length || d.stored.length);
  const recent = c.state !== "idle" && c.age < 60000;
  box.style.display = !demo && d && d.clearEnabled && (hasCodes || recent) ? "block" : "none";

  const engineOff = Date.now() - lastLive.at < 2000 && lastLive.rpm === 0;
  const busy = c.state === "pending" || c.state === "sent";
  btn.disabled = !engineOff || busy || !hasCodes;

  let text = "";
  if (c.state === "pending") text = "Đang gửi lệnh xóa tới ECU…";
  else if (c.state === "sent") text = "ECU đã nhận lệnh xóa, đang đọc lại mã lỗi để kiểm tra…";
  else if (recent && c.state === "ok")
    text = "Đã xóa xong, ECU không còn mã lỗi. Tắt khóa rồi bật lại, đèn FI sẽ tắt.";
  else if (recent && c.state === "failed")
    text = CLEAR_MSG[c.msg] || `ECU từ chối lệnh xóa (${c.msg}).`;
  else if (clearLocalMsg) text = clearLocalMsg;
  else if (hasCodes && !engineOff) text = "Tắt máy, để khóa ở ON để xóa lỗi.";
  else if (hasCodes) text = "Xóa toàn bộ mã lỗi (đang xảy ra và đã lưu) khỏi ECU.";
  msg.textContent = text;
}

async function clearCodes() {
  if (!confirm("Xóa toàn bộ mã lỗi khỏi ECU?\n\nMã lỗi đã lưu sẽ mất và đèn FI sẽ tắt. " +
               "Nếu lỗi vẫn còn (ví dụ giắc chưa cắm chặt), ECU sẽ báo lại ngay.")) return;
  clearLocalMsg = "Đang gửi yêu cầu…";
  renderClear();
  try {
    const r = await fetch("/dtc/clear", { method:"POST" });
    const j = await r.json();
    clearLocalMsg = j.state === "pending" ? "" : (CLEAR_MSG[j.msg] || j.msg);
  } catch (e) {
    clearLocalMsg = "Mất WiFi tới ESP32.";
  }
  clearUntil = Date.now() + 15000;
  pollDtc();
}

function route() { document.body.classList.toggle("show-dtc", location.hash === "#dtc"); }
window.addEventListener("hashchange", route);
route();

let dtcTimer = null, dtcGen = 0;
async function pollDtc() {
  clearTimeout(dtcTimer);
  const gen = ++dtcGen;  // a newer call supersedes this one (only one polling loop runs)
  try {
    const r = await fetch("/dtc", { cache:"no-store" });
    const j = await r.json();
    if (gen !== dtcGen) return;
    dtcData = j;
    if (j.clear && j.clear.state !== "idle" && j.clear.age < 60000) clearLocalMsg = "";
  } catch (e) { /* keep the last result; the main status shows WiFi loss */ }
  if (gen !== dtcGen) return;
  renderDtc();
  dtcTimer = setTimeout(pollDtc, Date.now() < clearUntil ? 1000 : 5000);
}
pollDtc();
</script>
</body>
</html>
)HTML";
