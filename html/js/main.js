/** FlashSafe Pro browser front end for tools/webui.py. */

const API = location.protocol === "file:" ? "http://127.0.0.1:8765" : "";
const $ = (id) => document.getElementById(id);
const projectConsole = $("console");
const deviceConsole = $("device-console");
let lastProjectLogs = 0;
let lastDeviceLogs = 0;
let serverOnline = false;

function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, (char) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
  })[char]);
}

function fmtSize(bytes) {
  if (bytes < 1024) return bytes + " B";
  if (bytes < 1048576) return (bytes / 1024).toFixed(1) + " KB";
  return (bytes / 1048576).toFixed(2) + " MB";
}

async function request(path, options) {
  const response = await fetch(API + path, options);
  const data = await response.json().catch(() => ({}));
  if (!response.ok || data.error) throw new Error(data.error || "请求失败");
  return data;
}

async function fetchStatus(timeoutMs = 2500) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    return await request("/api/status", { signal: controller.signal });
  } finally {
    clearTimeout(timer);
  }
}

function renderLog(target, logs, lastCount, emptyText) {
  if (!logs || logs.length === 0) {
    target.classList.add("empty");
    target.dataset.empty = emptyText;
    return 0;
  }
  const nearBottom = target.scrollHeight - target.scrollTop - target.clientHeight < 40;
  if (logs.length < lastCount) {
    target.innerHTML = "";
    lastCount = 0;
  }
  for (let index = lastCount; index < logs.length; index += 1) {
    const line = logs[index];
    const div = document.createElement("div");
    const cls = /error|fail|invalid|错误|失败|拒绝|hardfault/i.test(line.m) ? "err" :
      (/ok|success|complete|完成|通过|valid|connected/i.test(line.m) ? "ok" : "");
    div.className = "line" + (cls ? " " + cls : "");
    div.innerHTML = '<span class="t">' + escapeHtml(line.t) + "</span>" + escapeHtml(line.m);
    target.appendChild(div);
  }
  target.classList.remove("empty");
  if (nearBottom) target.scrollTop = target.scrollHeight;
  return logs.length;
}

function updateSerialPorts(serialState) {
  const select = $("serial-port");
  const old = select.value || "COM3";
  const ports = serialState.ports || [];
  const values = ports.map((item) => item.device);
  if (!values.includes(old)) values.unshift(old);
  select.innerHTML = values.map((port) => {
    const record = ports.find((item) => item.device === port);
    const label = record ? port + " — " + record.description : port;
    return '<option value="' + escapeHtml(port) + '">' + escapeHtml(label) + "</option>";
  }).join("");
  select.value = serialState.port || old;
}

function renderFiles(data) {
  const files = [];
  (data.pkgs || []).forEach((item) => files.push({
    name: item.name, meta: fmtSize(item.size) + " · " + item.mtime,
    href: API + "/api/download/pkg?name=" + encodeURIComponent(item.name)
  }));
  (data.releases || []).forEach((item) => files.push({
    name: item.name, meta: "v" + item.version + " · 发布清单",
    href: API + "/api/download/release?name=" + encodeURIComponent(item.name)
  }));
  files.push({ name: "public_key.h", meta: "当前设备内置公钥", href: API + "/api/download/key" });
  files.push({ name: "private_key.pem", meta: "签名私钥（需要确认）",
               href: API + "/api/download/private?confirm=1", confirm: true });
  $("files").innerHTML = files.map((item) =>
    '<div class="file"><div><div class="name">' + escapeHtml(item.name) +
    '</div><div class="meta">' + escapeHtml(item.meta) + "</div></div>" +
    '<button class="btn ghost" data-href="' + item.href + '"' +
    (item.confirm ? ' data-confirm="1"' : "") + ">下载</button></div>"
  ).join("");
  document.querySelectorAll("#files button").forEach((button) => {
    button.onclick = () => {
      if (button.dataset.confirm && !confirm("私钥仅限本机安全使用，确认下载？")) return;
      window.location.href = button.dataset.href;
    };
  });
}

function setOnline(data) {
  serverOnline = true;
  $("conn-pill").textContent = "本地服务：在线";
  $("conn-pill").className = "pill online";
  const serialState = data.serial || {};
  $("serial-state").textContent = serialState.connected
    ? serialState.port + " @ " + serialState.baud : "未连接";
  $("btn-serial-connect").disabled = !!serialState.connected;
  $("btn-serial-close").disabled = !serialState.connected;
  $("btn-serial-send").disabled = !serialState.connected;
  $("btn-ota-send").disabled = !serialState.connected;
  $("btn-enter-cli").disabled = !serialState.connected;
  updateSerialPorts(serialState);
  lastDeviceLogs = renderLog(deviceConsole, serialState.logs, lastDeviceLogs, "连接串口后，设备日志会显示在这里");
  const firmwareServer = data.firmware_server || {};
  $("fw-server-state").textContent = firmwareServer.running
    ? "运行中：端口 " + firmwareServer.port : "未启动";
  $("btn-fw-server").disabled = !!firmwareServer.running;
  $("btn-fw-server-stop").disabled = !firmwareServer.running;
  $("pkg-count").textContent = (data.pkgs || []).length;
  const latest = (data.releases || [])[0];
  $("last-run").textContent = latest ? "v" + latest.version + " · " + latest.generated_utc : "—";
  $("key-hash").textContent = "当前公钥：" + (data.key_sha256 || "未找到");
  $("btn-release").disabled = !!(data.release || {}).running;
  $("btn-flash").disabled = !!(data.flash || {}).running;
  $("btn-rotate").disabled = !!data.running;
  lastProjectLogs = renderLog(projectConsole, data.logs, lastProjectLogs, "等待发布、服务器或烧录任务…");
  renderFiles(data);
}

function setOffline() {
  serverOnline = false;
  $("conn-pill").textContent = "本地服务：未运行";
  $("conn-pill").className = "pill offline";
  ["btn-serial-connect", "btn-serial-close", "btn-serial-send", "btn-ota-send", "btn-enter-cli",
   "btn-fw-server", "btn-fw-server-stop", "btn-release", "btn-flash", "btn-rotate"].forEach((id) => {
    $(id).disabled = true;
  });
}

async function post(path, payload) {
  if (!serverOnline) throw new Error("请先运行 python tools\\webui.py");
  return request(path, { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload || {}) });
}

async function connectSerial() {
  try {
    await post("/api/serial/open", { port: $("serial-port").value, baud: $("serial-baud").value });
    await refresh();
  } catch (error) { alert(error.message); }
}

async function sendCommand(command) {
  const text = (command || $("serial-command").value).trim();
  if (!text) return;
  try {
    await post("/api/serial/send", { text, newline: true });
    $("serial-command").value = "";
  } catch (error) { alert(error.message); }
}

async function startRelease() {
  const version = $("version").value.trim();
  if (!/^\d+\.\d+\.\d+$/.test(version)) return alert("版本号格式应为 X.Y.Z");
  try { await post("/api/release", { version }); } catch (error) { alert(error.message); }
}

async function startFirmwareServer() {
  try { await post("/api/fw-server/start", { port: $("fw-server-port").value }); }
  catch (error) { alert(error.message); }
}

async function flash() {
  const target = $("flash-target").value;
  const port = $("serial-port").value;
  const hint = target === "bootloader"
    ? "确认 BOOT0 已接 3.3V，并且刚按过复位键进入 STM32 ROM 下载模式？"
    : "确认要通过 STM32 ROM 串口下载模式烧录 " + target + "？";
  if (!confirm(hint)) return;
  try { await post("/api/flash", { target, port }); } catch (error) { alert(error.message); }
}

async function sendOta() {
  const url = $("ota-url").value.trim().replace(/^https?:\/\//, "");
  if (!url || !url.includes("/")) return alert("请输入例如 192.168.137.1:8000/firmware_v1.2.3_slotB.pkg 的 URL");
  if (!confirm("确认此包的目标槽与当前 ACTIVE 槽相反？")) return;
  await sendCommand("upgrade net " + url);
}

async function rotateKeys() {
  const version = $("version").value.trim();
  if (!/^\d+\.\d+\.\d+$/.test(version)) return alert("版本号格式应为 X.Y.Z");
  if (!confirm("将轮换签名密钥并重建所有固件，确认继续？")) return;
  try { await post("/api/rotate", { version }); } catch (error) { alert(error.message); }
}

async function refresh() {
  try { setOnline(await fetchStatus()); } catch (error) { setOffline(); }
}

$("btn-serial-refresh").onclick = refresh;
$("btn-serial-connect").onclick = connectSerial;
$("btn-serial-close").onclick = async () => { try { await post("/api/serial/close"); } catch (error) { alert(error.message); } };
$("btn-serial-send").onclick = () => sendCommand();
$("serial-command").onkeydown = (event) => { if (event.key === "Enter") sendCommand(); };
$("btn-enter-cli").onclick = () => sendCommand("x");
document.querySelectorAll(".quick-command").forEach((button) => { button.onclick = () => sendCommand(button.dataset.command); });
$("btn-ota-send").onclick = () => sendOta().catch((error) => alert(error.message));
$("btn-release").onclick = startRelease;
$("btn-fw-server").onclick = startFirmwareServer;
$("btn-fw-server-stop").onclick = async () => { try { await post("/api/fw-server/stop"); } catch (error) { alert(error.message); } };
$("btn-flash").onclick = flash;
$("btn-rotate").onclick = rotateKeys;

async function loop() { await refresh(); setTimeout(loop, 1200); }
loop();
