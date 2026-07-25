#ifndef WEB_MODULE_H
#define WEB_MODULE_H

const char pageLive_P[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="vi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Smart Charger Dashboard</title>
<style>
:root{
  --bg:#f7f4ef;
  --ink:#1d2a35;
  --muted:#5f6f7c;
  --card:#ffffff;
  --brand:#0b7d6c;
  --brand-2:#ff8a3d;
  --danger:#c23934;
  --ring:rgba(11,125,108,.25);
  --shadow:0 12px 28px rgba(20,35,55,.08);
}
*{box-sizing:border-box}
body{
  margin:0;
  font-family:"Segoe UI",Tahoma,sans-serif;
  color:var(--ink);
  background:
    radial-gradient(circle at 10% 10%, #ffe8d1 0, transparent 34%),
    radial-gradient(circle at 90% 0%, #dff6f1 0, transparent 32%),
    var(--bg);
  min-height:100svh;
}
.wrap{max-width:980px;margin:0 auto;padding:18px 14px 26px}
.hero{
  background:linear-gradient(145deg,#0b7d6c,#0f5f75 60%,#214f75);
  color:#fff;
  border-radius:18px;
  padding:18px;
  box-shadow:var(--shadow);
}
.hero h1{margin:0;font-size:clamp(22px,5vw,30px);letter-spacing:.3px}
.hero p{margin:8px 0 0;color:rgba(255,255,255,.86)}
.grid{
  margin-top:14px;
  display:grid;
  grid-template-columns:repeat(auto-fit,minmax(210px,1fr));
  gap:10px;
}
.tile{
  background:var(--card);
  border-radius:14px;
  box-shadow:var(--shadow);
  padding:12px;
  border:1px solid #eef2f6;
}
.k{font-size:13px;color:var(--muted)}
.v{margin-top:4px;font-size:24px;font-weight:700;line-height:1.1}
.status{
  display:inline-flex;
  gap:8px;
  align-items:center;
  margin-top:8px;
  font-weight:700;
  font-size:14px;
}
.dot{width:10px;height:10px;border-radius:999px;background:#9ca3af}
.actions{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin-top:14px}
button{
  border:0;
  padding:12px 10px;
  border-radius:12px;
  color:#fff;
  font-weight:700;
  cursor:pointer;
  box-shadow:var(--shadow);
}
button:focus-visible{outline:3px solid var(--ring);outline-offset:2px}
.btn-wait{background:linear-gradient(180deg,#0ea5e9,#0b7fc0)}
.btn-now{background:linear-gradient(180deg,#16a34a,#0f7f39)}
.btn-stop{background:linear-gradient(180deg,#ef4444,#c23934)}
.footer{display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:wrap;margin-top:14px}
.link{
  display:inline-block;
  text-decoration:none;
  font-weight:700;
  color:var(--brand);
  background:#ffffffb8;
  border:1px solid #dce9ef;
  border-radius:10px;
  padding:9px 12px;
}
.small{font-size:12px;color:var(--muted)}
@media (max-width:680px){
  .actions{grid-template-columns:1fr}
  .v{font-size:22px}
}
</style>
</head>
<body>
<div class="wrap">
  <section class="hero">
    <h1>Smart Charger</h1>
    <p>Bang dieu khien toi uu cho dien thoai, tablet va may tinh</p>
    <div class="status"><span class="dot" id="dot"></span><span id="st">Dang tai...</span></div>
  </section>

  <section class="grid">
    <article class="tile"><div class="k">Nhiet bo sac (DS)</div><div class="v"><span id="ds">--</span> C</div></article>
    <article class="tile"><div class="k">Nhiet vo binh (MLX)</div><div class="v"><span id="mlx">--</span> C</div></article>
    <article class="tile"><div class="k">Nhiet moi truong</div><div class="v"><span id="dht_t">--</span> C</div></article>
    <article class="tile"><div class="k">Do am moi truong</div><div class="v"><span id="dht_h">--</span> %</div></article>
    <article class="tile"><div class="k">Dien ap</div><div class="v"><span id="v">--</span> V</div></article>
    <article class="tile"><div class="k">Dong dien</div><div class="v"><span id="i">--</span> A</div></article>
    <article class="tile"><div class="k">Cong suat</div><div class="v"><span id="p">--</span> W</div></article>
    <article class="tile"><div class="k">Thoi gian da sac</div><div class="v"><span id="tchg">--</span> phut</div></article>
    <article class="tile"><div class="k">Tong dien (Tat ca)</div><div class="v"><span id="e_total">--</span> kWh</div></article>
    <article class="tile"><div class="k">Dien (Full)</div><div class="v"><span id="e_full">--</span> kWh</div></article>
    <article class="tile"><div class="k">Dien (Quahiet)</div><div class="v"><span id="e_temp">--</span> kWh</div></article>
    <article class="tile"><div class="k">Dien (Quado am)</div><div class="v"><span id="e_humid">--</span> kWh</div></article>
    <article class="tile"><div class="k">So lan day</div><div class="v"><span id="cnt_full">--</span> lan</div></article>
    <article class="tile"><div class="k">So lan khac</div><div class="v"><span id="cnt_other">--</span> lan</div></article>
  </section>

  <section class="actions">
    <button class="btn-wait" onclick="postCmd('/charge_wait')">Sac cho</button>
    <button class="btn-now" onclick="postCmd('/charge_now')">Sac ngay</button>
    <button class="btn-stop" onclick="postCmd('/charge_stop')">Dung sac</button>
  </section>

  <div class="footer">
    <a href="/setting" class="link">Mo trang cau hinh</a>
    <span class="small">Cap nhat moi 1 giay</span>
  </div>
</div>

<script>
function setStatusBadge(state){
  const dot=document.getElementById('dot');
  const s=(state||'').toUpperCase();
  if(s.indexOf('SAC')>=0 && s.indexOf('CHO')<0){dot.style.background='#16a34a';return;}
  if(s.indexOf('NGAT')>=0 || s.indexOf('LOI')>=0){dot.style.background='#ef4444';return;}
  if(s.indexOf('DO')>=0){dot.style.background='#f59e0b';return;}
  dot.style.background='#6b7280';
}
function n(v,d){
  if(v==='N/A')return 'N/A';
  const x=Number(v);
  if(!Number.isFinite(x)) return '--';
  return x.toFixed(d);
}
async function postCmd(path){
  try{await fetch(path,{method:'POST'});}catch(e){}
  setTimeout(updateUI,250);
}
async function updateUI(){
  try{
    const r=await fetch('/data',{cache:'no-store'});
    const j=await r.json();
    document.getElementById('st').textContent=j.state||'--';
    setStatusBadge(j.state||'');
    document.getElementById('ds').textContent=n(j.ds,1);
    document.getElementById('mlx').textContent=n(j.mlx,1);
    document.getElementById('v').textContent=n(j.v,1);
    document.getElementById('i').textContent=n(j.i,2);
    document.getElementById('p').textContent=n(j.p,1);
    document.getElementById('dht_t').textContent=n(j.dht_t,1);
    document.getElementById('dht_h').textContent=n(j.dht_h,0);
    document.getElementById('tchg').textContent=n(j.charge_min,1);
    document.getElementById('e_total').textContent=n(j.energy_total,3);
    document.getElementById('e_full').textContent=n(j.energy_full,3);
    document.getElementById('e_temp').textContent=n(j.energy_temp,3);
    document.getElementById('e_humid').textContent=n(j.energy_humid,3);
    document.getElementById('cnt_full').textContent=j.count_full||0;
    document.getElementById('cnt_other').textContent=j.count_other||0;
  }catch(e){}
}
setInterval(updateUI,1000);
updateUI();
</script>
</body>
</html>
)rawliteral";

#define CFG_BUF_SZ 10400
char cfgPageBuf[CFG_BUF_SZ];

static void buildConfigPage() {
  int waitv = prefs.getInt("wait", DEFAULT_WAIT_MIN);
  int measurev = prefs.getInt("measure", DEFAULT_MEASURE_SEC);
  float minPower = prefs.getFloat("p_min", prefs.getFloat("full", DEFAULT_MIN_POWER_W));
  float maxPower = prefs.getFloat("p_max", DEFAULT_MAX_POWER_W);
  float tds_max = prefs.getFloat("t_ds", DEFAULT_MAX_TEMP_DS);
  float tmlx_max = prefs.getFloat("t_mlx", DEFAULT_MAX_TEMP_MLX);
  float tenv_max = prefs.getFloat("t_env", DEFAULT_MAX_TEMP_ENV);
  float hmax = prefs.getFloat("h_max", DEFAULT_MAX_HUMIDITY);
  float tds_warn = prefs.getFloat("tw_ds", DEFAULT_WARN_TEMP_DS);
  float tmlx_warn = prefs.getFloat("tw_mlx", DEFAULT_WARN_TEMP_MLX);
  float tenv_warn = prefs.getFloat("tw_env", DEFAULT_WARN_TEMP_ENV);
  int maxh = prefs.getInt("max_h", DEFAULT_MAX_CHARGE_HOURS);
  String apiKey = prefs.getString("api_key", station_token);
  String lcdName = prefs.getString("lcd_name", custom_station_name);
  int lcdUseServer = prefs.getInt("lcd_srv", lcd_name_from_server ? 1 : 0);

  cfg_ssid[sizeof(cfg_ssid) - 1] = '\0';
  cfg_pass[sizeof(cfg_pass) - 1] = '\0';

  const char *tpl = R"CFG(
<!doctype html>
<html lang="vi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Cau hinh Smart Charger</title>
<style>
:root{
  --bg:#f4f7fb;
  --card:#ffffff;
  --line:#dbe5ef;
  --ink:#112236;
  --muted:#5f7288;
  --brand:#1363a3;
  --brand2:#0b8f72;
}
*{box-sizing:border-box}
body{
  margin:0;
  font-family:"Segoe UI",Tahoma,sans-serif;
  color:var(--ink);
  background:
    radial-gradient(1200px 350px at -10%% -20%%, #d9f7ee 0, transparent 45%%),
    radial-gradient(1200px 300px at 100%% 0%%, #dfefff 0, transparent 45%%),
    var(--bg);
  min-height:100svh;
}
.wrap{max-width:960px;margin:0 auto;padding:16px 12px 24px}
.head{margin-bottom:12px}
.head h1{margin:0;font-size:clamp(21px,4.4vw,30px)}
.head p{margin:6px 0 0;color:var(--muted)}
.card{
  background:var(--card);
  border:1px solid var(--line);
  border-radius:16px;
  box-shadow:0 14px 30px rgba(16,36,64,.08);
  padding:14px;
}
.section{padding:6px 0 2px}
.section h2{margin:8px 0 4px;font-size:17px}
.desc{margin:0;color:var(--muted);font-size:13px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px;margin-top:10px}
.field{display:flex;flex-direction:column;gap:5px}
label{font-size:13px;font-weight:700;color:#233648}
input[type=text],input[type=password],input[type=number]{
  width:100%%;
  border:1px solid var(--line);
  border-radius:10px;
  padding:10px 11px;
  font-size:14px;
  background:#fff;
}
input:focus{outline:2px solid #b8d8ef;border-color:#95bfdc}
.chk{display:flex;align-items:center;gap:8px;margin-top:8px}
.chk input{width:auto}
.tabs{display:flex;gap:6px;margin-bottom:16px;border-bottom:2px solid var(--line);padding-bottom:8px;overflow-x:auto}
.tab-btn{background:none;border:none;color:var(--muted);padding:8px 12px;font-weight:700;cursor:pointer;border-radius:8px}
.tab-btn.active{background:var(--brand);color:#fff;box-shadow:0 6px 14px rgba(19,99,163,.16)}
.tab-section{display:none}
.tab-section.active{display:block}
.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px}
button,a.btn{
  border:0;
  border-radius:12px;
  padding:11px 14px;
  font-weight:700;
  text-decoration:none;
  cursor:pointer;
}
button{background:linear-gradient(180deg,var(--brand),#0f4f81);color:#fff}
button.alt{background:linear-gradient(180deg,var(--brand2),#08705a)}
a.btn{background:#ecf5ff;color:#0b4a7f;border:1px solid #c8deef}
small{font-size:12px;color:var(--muted)}
hr{border:none;border-top:1px dashed var(--line);margin:12px 0}
@media (max-width:640px){
  .actions{display:grid;grid-template-columns:1fr}
}
</style>
</head>
<body>
<div class="wrap">
  <header class="head">
    <h1>Cau hinh o cam sac xe dien</h1>
    <p>Giao dien responsive, chay hoan toan noi bo khong can Internet</p>
  </header>

  <form class="card" method="POST" action="/save">
    <div class="tabs">
      <button type="button" class="tab-btn active" onclick="showTab('sec-conn', this)">Kết nối</button>
      <button type="button" class="tab-btn" onclick="showTab('sec-cycle', this)">Chu trình</button>
      <button type="button" class="tab-btn" onclick="showTab('sec-safety', this)">An toàn</button>
      <button type="button" class="tab-btn" onclick="showTab('sec-features', this)">Tính năng</button>
      <button type="button" class="tab-btn" onclick="showTab('sec-pins', this)">Chân Pin</button>
    </div>

    <div id="sec-conn" class="tab-section active">
      <section class="section">
        <h2>Ket noi</h2>
        <p class="desc">AP phat cau hinh, WiFi hoat dong va thong tin cloud</p>
        <div class="grid">
          <div class="field"><label>AP SSID</label><input name="ap_ssid" type="text" value="%s"></div>
          <div class="field"><label>AP Password (>=8 ky tu)</label><input name="ap_pass" type="text" value="%s"></div>
          <div class="field"><label>SSID WiFi</label><input name="ssid" type="text" value="%s"></div>
          <div class="field"><label>Password WiFi</label><input name="pass" type="password" placeholder="De trong neu khong doi"></div>
          <div class="field"><label>API Key tram (Station Token)</label><input name="api_key" type="password" value="%s"></div>
          <div class="field"><label>Telegram (chat_id:token)</label><input name="tg_mix" type="text" placeholder="VD: 123456789:AAxxxx"></div>
          <div class="field"><label>Ten tram LCD</label><input name="lcd_name" type="text" value="%s" placeholder="De trong de dung ten server"></div>
        </div>
        <label class="chk"><input name="lcd_srv" type="checkbox" value="1" %s>Uu tien ten tram tu server</label>
      </section>
    </div>

    <div id="sec-cycle" class="tab-section">
      <section class="section">
        <h2>Chu trinh sac</h2>
        <div class="grid">
          <div class="field"><label>Cho truoc khi sac (phut)</label><input name="wait" type="number" min="0" value="%d"></div>
          <div class="field"><label>Xac nhan day (giay)</label><input name="measure" type="number" min="0" value="%d"><small>0 = tat dieu kien day</small></div>
          <div class="field"><label>P toi thieu / nguong day (W)</label><input name="p_min" type="number" step="0.1" min="0" value="%.1f"></div>
          <div class="field"><label>P toi da (W)</label><input name="p_max" type="number" step="0.1" min="0" value="%.1f"></div>
          <div class="field"><label>Gioi han thoi gian sac (gio)</label><input name="max_h" type="number" min="0" value="%d"></div>
        </div>
      </section>
    </div>

    <div id="sec-safety" class="tab-section">
      <section class="section">
        <h2>An toan nhiet va moi truong</h2>
        <p class="desc">Nhap 0 de bo qua tung gioi han tuong ung</p>
        <div class="grid">
          <div class="field"><label>Nhiet moi truong toi da (C)</label><input name="t_env" type="number" step="0.1" value="%.1f"></div>
          <div class="field"><label>Canh bao som moi truong (C)</label><input name="tw_env" type="number" step="0.1" value="%.1f"></div>
          <div class="field"><label>Do am toi da (%%)</label><input name="h_max" type="number" step="1" value="%.0f"></div>
          <div class="field"><label>Nhiet bo sac toi da DS (C)</label><input name="t_ds" type="number" step="0.1" value="%.1f"></div>
          <div class="field"><label>Canh bao som DS (C)</label><input name="tw_ds" type="number" step="0.1" value="%.1f"></div>
          <div class="field"><label>Nhiet vo binh toi da MLX (C)</label><input name="t_mlx" type="number" step="0.1" value="%.1f"></div>
          <div class="field"><label>Canh bao som MLX (C)</label><input name="tw_mlx" type="number" step="0.1" value="%.1f"></div>
        </div>
      </section>
    </div>

    <div id="sec-features" class="tab-section">
      <section class="section">
        <h2>Bat/Tat Tinh nang & Cam bien</h2>
        <div class="grid">
          <label class="chk"><input name="feat_ds" type="checkbox" value="1" %s>Cam bien bo sac (DS18B20)</label>
          <label class="chk"><input name="feat_mlx" type="checkbox" value="1" %s>Cam bien pin (MLX90614)</label>
          <label class="chk"><input name="feat_dht" type="checkbox" value="1" %s>Cam bien moi truong (DHT)</label>
          <label class="chk"><input name="feat_pzem" type="checkbox" value="1" %s>Cam bien nguon (PZEM)</label>
          <label class="chk"><input name="feat_tg" type="checkbox" value="1" %s>Telegram alerts/commands</label>
          <label class="chk"><input name="feat_cloud" type="checkbox" value="1" %s>Dong bo Cloud Server</label>
          <label class="chk"><input name="feat_ap_on" type="checkbox" value="1" %s>Phat WiFi AP de cau hinh</label>
          <label class="chk"><input name="feat_ap_always" type="checkbox" value="1" %s>Luon phat AP WiFi (khong tat sau 5 phut)</label>
        </div>
      </section>
    </div>

    <div id="sec-pins" class="tab-section">
      <section class="section">
        <h2>Cau hinh chan cam bien & thiet bi</h2>
        <p class="desc">Chinh sua cac chan ket noi physical cua vi dieu khien</p>
        <div class="grid">
          <div class="field"><label>DS18B20 Pin (Nhiệt sạc)</label><input name="ds_pin" type="number" min="0" max="39" value="%d"></div>
          <div class="field"><label>MLX90614 SDA Pin</label><input name="m_sda" type="number" min="0" max="39" value="%d"></div>
          <div class="field"><label>MLX90614 SCL Pin</label><input name="m_scl" type="number" min="0" max="39" value="%d"></div>
          <div class="field"><label>DHT Pin (Nhiệt/Ẩm mt)</label><input name="d_pin" type="number" min="0" max="39" value="%d"></div>
          <div class="field"><label>PZEM RX Pin</label><input name="p_rx" type="number" min="0" max="39" value="%d"></div>
          <div class="field"><label>PZEM TX Pin</label><input name="p_tx" type="number" min="0" max="39" value="%d"></div>
          <div class="field"><label>LCD SDA Pin</label><input name="l_sda" type="number" min="0" max="39" value="%d"></div>
          <div class="field"><label>LCD SCL Pin</label><input name="l_scl" type="number" min="0" max="39" value="%d"></div>
          <div class="field"><label>RTC SDA Pin</label><input name="r_sda" type="number" min="0" max="39" value="%d"></div>
          <div class="field"><label>RTC SCL Pin</label><input name="r_scl" type="number" min="0" max="39" value="%d"></div>
          <div class="field"><label>Relay Sạc Pin</label><input name="rel_pin" type="number" min="0" max="39" value="%d"></div>
          <div class="field"><label>Nút sạc ngay/dừng (BTN3)</label><input name="btn_pin" type="number" min="0" max="39" value="%d"></div>
          <div class="field"><label>Nút Reset (BTN_RESTORE)</label><input name="btn_rst" type="number" min="0" max="39" value="%d"></div>
        </div>
      </section>
    </div>

    <hr>

    <div class="actions">
      <button type="submit">Luu cau hinh</button>
      <button type="button" class="alt" onclick="if(confirm('Khoi phuc cai dat goc?')) fetch('/reset').then(()=>{alert('Da khoi phuc, thiet bi se khoi dong lai');});">Khoi phuc cai dat</button>
      <button type="button" class="alt" onclick="if(confirm('Khoi dong lai thiet bi?')) fetch('/reboot').then(()=>{alert('Thiet bi dang khoi dong lai');});">Reboot</button>
      <a class="btn" href="/">Ve dashboard</a>
    </div>
  </form>
</div>
<script>
function showTab(secId, btn) {
  var sections = document.getElementsByClassName('tab-section');
  for (var i = 0; i < sections.length; i++) {
    sections[i].style.display = 'none';
  }
  var btns = document.getElementsByClassName('tab-btn');
  for (var i = 0; i < btns.length; i++) {
    btns[i].classList.remove('active');
  }
  document.getElementById(secId).style.display = 'block';
  btn.classList.add('active');
}
</script>
</body>
</html>
)CFG";

  snprintf(cfgPageBuf, CFG_BUF_SZ, tpl,
           ap_ssid,
           ap_pass,
           cfg_ssid,
           apiKey.c_str(),
           lcdName.c_str(),
           lcdUseServer ? "checked" : "",
           waitv, measurev,
           minPower,
           maxPower,
           maxh,
           tenv_max,
           tenv_warn,
           hmax,
           tds_max,
           tds_warn,
           tmlx_max,
           tmlx_warn,
           feat_ds ? "checked" : "",
           feat_mlx ? "checked" : "",
           feat_dht ? "checked" : "",
           feat_pzem ? "checked" : "",
           feat_tg ? "checked" : "",
           feat_cloud ? "checked" : "",
           feat_ap_on ? "checked" : "",
           feat_ap_always ? "checked" : "",
           ds18b20_pin,
           mlx_sda_pin,
           mlx_scl_pin,
           dht_pin,
           pzem_rx_pin,
           pzem_tx_pin,
           lcd_sda_pin,
           lcd_scl_pin,
           rtc_sda_pin,
           rtc_scl_pin,
           relay_pin,
           btn_pin,
           btn_restore_pin);
}

static void handleSetting() {
  buildConfigPage();
  server.send(200, "text/html", cfgPageBuf);
}

static void handleRoot() {
  server.send_P(200, "text/html", pageLive_P);
}

static void handleSave() {
  String aps = server.arg("ap_ssid");
  String app = server.arg("ap_pass");
  String s = server.arg("ssid");
  String p = server.arg("pass");
  String api_key = server.arg("api_key");
  String tg_mix = server.arg("tg_mix");
  String lcd_name = server.arg("lcd_name");

  aps.trim();
  app.trim();

  if (aps.length() > 0) {
    prefs.putString("ap_ssid", aps);
    aps.toCharArray(ap_ssid, sizeof(ap_ssid));
  }

  if (app.length() >= 8) {
    prefs.putString("ap_pass", app);
    app.toCharArray(ap_pass, sizeof(ap_pass));
  }

  prefs.putString("ssid", s);
  s.toCharArray(cfg_ssid, sizeof(cfg_ssid));

  if (p.length() > 0) {
    prefs.putString("pass", p);
    p.toCharArray(cfg_pass, sizeof(cfg_pass));
  }

  api_key.trim();
  if (api_key.length() > 0) {
    prefs.putString("api_key", api_key);
    api_key.toCharArray(station_token, sizeof(station_token));
  }

  if (tg_mix.length() > 0) {
    prefs.putString("tg_mix", tg_mix);
    parseTgMix(tg_mix);
  }

  lcd_name.trim();
  prefs.putString("lcd_name", lcd_name);
  strlcpy(custom_station_name, lcd_name.c_str(), sizeof(custom_station_name));
  int lcdSrv = server.hasArg("lcd_srv") ? 1 : 0;
  prefs.putInt("lcd_srv", lcdSrv);
  lcd_name_from_server = (lcdSrv == 1);

  long waitValue = server.arg("wait").toInt();
  if (waitValue < 0) waitValue = 0;
  if (waitValue > 65535) waitValue = 65535;
  prefs.putInt("wait", (int)waitValue);

  long measureValue = server.arg("measure").toInt();
  if (measureValue < 0) measureValue = 0;
  if (measureValue > 3600) measureValue = 3600;
  prefs.putInt("measure", (int)measureValue);
  float pMin = server.arg("p_min").length() ? server.arg("p_min").toFloat() : server.arg("full").toFloat();
  if (pMin < 0.0f) pMin = 0.0f;
  float pMax = server.arg("p_max").toFloat();
  if (pMax < 0.0f) pMax = 0.0f;
  float tDs = server.arg("t_ds").toFloat();
  if (tDs < 0.0f) tDs = 0.0f;
  float tMlx = server.arg("t_mlx").toFloat();
  if (tMlx < 0.0f) tMlx = 0.0f;
  float tEnv = server.arg("t_env").toFloat();
  if (tEnv < 0.0f) tEnv = 0.0f;
  float hMax = server.arg("h_max").toFloat();
  if (hMax < 0.0f) hMax = 0.0f;
  float twDs = server.arg("tw_ds").toFloat();
  if (twDs < 0.0f) twDs = 0.0f;
  float twMlx = server.arg("tw_mlx").toFloat();
  if (twMlx < 0.0f) twMlx = 0.0f;
  float twEnv = server.arg("tw_env").toFloat();
  if (twEnv < 0.0f) twEnv = 0.0f;
  long maxHours = server.arg("max_h").toInt();
  if (maxHours < 0) maxHours = 0;
  if (maxHours > 72) maxHours = 72;
  prefs.putFloat("p_min", pMin);
  prefs.putFloat("full", pMin);
  prefs.putFloat("p_max", pMax);
  prefs.putFloat("t_ds", tDs);
  prefs.putFloat("t_mlx", tMlx);
  prefs.putFloat("t_env", tEnv);
  prefs.putFloat("h_max", hMax);

  prefs.putFloat("tw_ds", twDs);
  prefs.putFloat("tw_mlx", twMlx);
  prefs.putFloat("tw_env", twEnv);
  prefs.putInt("max_h", (int)maxHours);

  bool fDs = server.hasArg("feat_ds");
  bool fMlx = server.hasArg("feat_mlx");
  bool fDht = server.hasArg("feat_dht");
  bool fPzem = server.hasArg("feat_pzem");
  bool fTg = server.hasArg("feat_tg");
  bool fCloud = server.hasArg("feat_cloud");
  bool fApOn = server.hasArg("feat_ap_on");
  bool fApAlw = server.hasArg("feat_ap_always");

  prefs.putBool("feat_ds", fDs);
  prefs.putBool("feat_mlx", fMlx);
  prefs.putBool("feat_dht", fDht);
  prefs.putBool("feat_pzem", fPzem);
  prefs.putBool("feat_tg", fTg);
  prefs.putBool("feat_cloud", fCloud);
  prefs.putBool("feat_ap_on", fApOn);
  prefs.putBool("feat_ap_alw", fApAlw);

  prefs.putUChar("ds_pin", server.arg("ds_pin").toInt());
  prefs.putUChar("m_sda", server.arg("m_sda").toInt());
  prefs.putUChar("m_scl", server.arg("m_scl").toInt());
  prefs.putUChar("d_pin", server.arg("d_pin").toInt());
  prefs.putUChar("p_rx", server.arg("p_rx").toInt());
  prefs.putUChar("p_tx", server.arg("p_tx").toInt());
  prefs.putUChar("l_sda", server.arg("l_sda").toInt());
  prefs.putUChar("l_scl", server.arg("l_scl").toInt());
  prefs.putUChar("r_sda", server.arg("r_sda").toInt());
  prefs.putUChar("r_scl", server.arg("r_scl").toInt());
  prefs.putUChar("rel_pin", server.arg("rel_pin").toInt());
  prefs.putUChar("btn_pin", server.arg("btn_pin").toInt());
  prefs.putUChar("btn_rst", server.arg("btn_rst").toInt());

  server.send(200, "text/html; charset=utf-8", "<meta charset=\"utf-8\">Da luu cau hinh. Thiet bi se khoi dong lai...");
  delay(350);
  ESP.restart();
}

static void handleData() {
  SensorFrame s = readSensorFrame();
  unsigned long elapsedMin = chargeElapsedMs() / 60000UL;
  long nextStartSec = 0;
  if (autoEnabled && nextStartMs != 0 && (long)(nextStartMs - millis()) > 0) {
    nextStartSec = (long)((nextStartMs - millis()) / 1000UL);
  }

  char ds_str[16], mlx_str[16], dht_t_str[16], dht_h_str[16], v_str[16], i_str[16], p_str[16];
  if (feat_ds) snprintf(ds_str, sizeof(ds_str), "%.2f", safeReading(s.tds)); else strlcpy(ds_str, "\"N/A\"", sizeof(ds_str));
  if (feat_mlx) snprintf(mlx_str, sizeof(mlx_str), "%.2f", safeReading(s.tmlx)); else strlcpy(mlx_str, "\"N/A\"", sizeof(mlx_str));
  if (feat_dht) {
    snprintf(dht_t_str, sizeof(dht_t_str), "%.2f", safeReading(s.tenv));
    snprintf(dht_h_str, sizeof(dht_h_str), "%.2f", safeReading(s.hum));
  } else {
    strlcpy(dht_t_str, "\"N/A\"", sizeof(dht_t_str));
    strlcpy(dht_h_str, "\"N/A\"", sizeof(dht_h_str));
  }
  if (feat_pzem) {
    snprintf(v_str, sizeof(v_str), "%.2f", safeReading(s.voltage));
    snprintf(i_str, sizeof(i_str), "%.3f", safeReading(s.current));
    snprintf(p_str, sizeof(p_str), "%.2f", safeReading(s.pwr));
  } else {
    strlcpy(v_str, "\"N/A\"", sizeof(v_str));
    strlcpy(i_str, "\"N/A\"", sizeof(i_str));
    strlcpy(p_str, "\"N/A\"", sizeof(p_str));
  }

  char jb[896];
  snprintf(jb, sizeof(jb),
           "{\"state\":\"%s\",\"state_key\":\"%s\",\"ds\":%s,\"mlx\":%s,\"dht_t\":%s,\"env_t\":%s,\"dht_h\":%s,\"hum\":%s,\"v\":%s,\"i\":%s,\"p\":%s,\"charge_min\":%.1f,\"relay_on\":%s,\"charging\":%s,\"auto_enabled\":%s,\"lockout\":%s,\"probe\":%s,\"wifi_rssi\":%d,\"next_start_sec\":%ld,\"energy_total\":%.3f,\"energy_full\":%.3f,\"energy_temp\":%.3f,\"energy_humid\":%.3f,\"count_full\":%u,\"count_other\":%u}",
           stateWebText(),
           stateToApiCode(),
           ds_str,
           mlx_str,
           dht_t_str,
           dht_t_str,
           dht_h_str,
           dht_h_str,
           v_str,
           i_str,
           p_str,
           (elapsedMin * 1.0f),
           digitalRead(RELAY_PIN) == HIGH ? "true" : "false",
           charging ? "true" : "false",
           autoEnabled ? "true" : "false",
           lockout ? "true" : "false",
           (probeState != PROBE_IDLE) ? "true" : "false",
           (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0,
           nextStartSec > 0 ? nextStartSec : 0L,
           total_energy_all,
           total_energy_full,
           total_energy_temp,
           total_energy_humid,
           (unsigned int)count_full,
           (unsigned int)count_not_full);
  server.send(200, "application/json", jb);
}

#endif
