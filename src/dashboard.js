import {
  controlDevice,
  getDeviceConfig,
  getDeviceControlStatus,
  getDevices,
  getHealth,
  saveDeviceConfig
} from "./services/api.js";

let devices = [];
let oledActive = true;
let clockModeActive = false;
let microphoneActive = true;
let speakerActive = true;
let oledSettingsDirty = false;
let controlDeviceId = "";
let refreshTimer = null;

function formatDate(value) {
  return value ? new Date(value).toLocaleString("pt-BR") : "—";
}

function formatUptime(totalSeconds) {
  const seconds = Math.max(0, Math.floor(totalSeconds));
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  return `${days}d ${hours}h ${minutes}min`;
}

function providerLabel(provider) {
  return provider === "nvidia" ? "NVIDIA" : "Google Gemini";
}

function setControlStatus(message, state = "normal") {
  const status = document.querySelector("#device-control-status");
  status.textContent = message;
  status.dataset.state = state;
}

function setControlsEnabled(enabled) {
  for (const id of [
    "control-device",
    "oled-welcome",
    "oled-contrast",
    "save-oled",
    "toggle-oled",
    "toggle-clock-mode",
    "toggle-microphone",
    "toggle-speaker",
    "restart-esp32",
    "esp-ssid",
    "esp-senha",
    "esp-url",
    "esp-token",
    "save-esp-config",
    "load-esp-config"
  ]) {
    document.querySelector(`#${id}`).disabled = !enabled;
  }
}

function selectedDeviceId() {
  return document.querySelector("#control-device").value;
}

async function loadDeviceConfig() {
  const deviceId = selectedDeviceId();
  if (!deviceId) return;
  try {
    const response = await getDeviceConfig(deviceId);
    const config = response.config || {};
    document.querySelector("#esp-ssid").value = config.ssid || "";
    document.querySelector("#esp-url").value = config.url || "";
    document.querySelector("#esp-senha").value = "";
    document.querySelector("#esp-token").value = "";
    setControlStatus(
      `ESP32 configurado: Wi-Fi "${config.ssid || "não definido"}" · servidor ${
        config.url || "não definido"
      } · token ${config.tokenConfigurado ? "configurado" : "não configurado"}.`
    );
  } catch (error) {
    setControlStatus(error.message, "error");
  }
}

async function refreshDeviceStatus() {
  const deviceId = selectedDeviceId();
  if (!deviceId) {
    setControlsEnabled(false);
    return;
  }
  setControlsEnabled(true);
  try {
    const response = await getDeviceControlStatus(deviceId);
    const device = response.status;
    oledActive = device.telaAtiva;
    clockModeActive = Boolean(device.modoHorario);
    microphoneActive = device.microfoneAtivo !== false;
    speakerActive = device.somAtivo !== false;
    if (controlDeviceId !== deviceId) {
      controlDeviceId = deviceId;
      oledSettingsDirty = false;
      await loadDeviceConfig();
    }
    if (!oledSettingsDirty) {
      document.querySelector("#oled-welcome").value = device.textoInicial || "";
      document.querySelector("#oled-contrast").value = String(device.contraste);
      document.querySelector("#oled-contrast-value").value = String(device.contraste);
    }
    document.querySelector("#toggle-oled").textContent =
      oledActive ? "Desligar tela OLED" : "Ligar tela OLED";
    document.querySelector("#toggle-clock-mode").textContent =
      clockModeActive ? "Desativar modo horário" : "Ativar modo horário";
    const microphoneButton = document.querySelector("#toggle-microphone");
    microphoneButton.textContent = microphoneActive
      ? "Desativar microfone"
      : "Ativar microfone";
    microphoneButton.setAttribute("aria-pressed", String(microphoneActive));
    const speakerButton = document.querySelector("#toggle-speaker");
    speakerButton.textContent = speakerActive
      ? "Desativar som"
      : "Ativar som";
    speakerButton.setAttribute("aria-pressed", String(speakerActive));
    setControlStatus(
      `${device.telaAtiva ? "OLED ligada" : "OLED desligada"} · ${
        device.modoHorario ? "modo horário ativo" : "modo horário desligado"
      } · microfone ${microphoneActive ? "ativo" : "desativado"} · som ${
        speakerActive ? "ativo" : "desativado"
      }${device.falando ? " · falando" : ""}`
    );
  } catch (error) {
    setControlStatus(error.message, "error");
  }
}

async function sendDeviceControl(action, values = {}) {
  const deviceId = selectedDeviceId();
  if (!deviceId) {
    setControlStatus("Nenhum ESP32 conectado ao dashboard.", "error");
    return;
  }
  setControlStatus("Enviando comando ao ESP32...", "pending");
  try {
    const result = await controlDevice({ deviceId, action, ...values });
    if (action === "screen") oledSettingsDirty = false;
    setControlStatus(result.result.mensagem || "Comando enviado.", "success");
    if (action !== "reboot") await refreshDeviceStatus();
  } catch (error) {
    setControlStatus(error.message, "error");
  }
}

async function saveDeviceConfigForm() {
  const deviceId = selectedDeviceId();
  if (!deviceId) {
    setControlStatus("Nenhum ESP32 conectado ao dashboard.", "error");
    return;
  }
  const payload = { deviceId };
  const ssid = document.querySelector("#esp-ssid").value.trim();
  const password = document.querySelector("#esp-senha").value;
  const url = document.querySelector("#esp-url").value.trim();
  const token = document.querySelector("#esp-token").value;
  if (ssid) payload.ssid = ssid;
  if (password) payload.senha = password;
  if (url) payload.url = url;
  if (token) payload.token = token;
  if (Object.keys(payload).length === 1) {
    setControlStatus("Preencha ao menos um campo para alterar.", "error");
    return;
  }
  setControlStatus("Salvando configuração no ESP32...", "pending");
  try {
    const response = await saveDeviceConfig(payload);
    document.querySelector("#esp-senha").value = "";
    document.querySelector("#esp-token").value = "";
    setControlStatus(
      response.result?.mensagem || "Configuração salva no ESP32.",
      "success"
    );
    await loadDeviceConfig();
    await refreshDeviceStatus();
  } catch (error) {
    setControlStatus(error.message, "error");
  }
}

function createDeviceCard(device) {
  const article = document.createElement("article");
  article.className = "card device-card";
  const heading = document.createElement("h3");
  heading.textContent = `Dispositivo ${device.deviceId}`;
  const statusLine = document.createElement("p");
  statusLine.className = "card-value connection-indicator";
  statusLine.dataset.status = device.status;
  const dot = document.createElement("span");
  dot.className = "dot";
  const statusText = document.createElement("span");
  statusText.textContent = device.status === "connected" ? "Conectado" : "Desconectado";
  statusLine.append(dot, statusText);
  const lastContact = document.createElement("p");
  lastContact.className = "card-detail";
  lastContact.textContent =
    `Último contato: ${formatDate(device.lastSeenAt)} (${device.secondsSinceLastSeen}s atrás)`;
  article.append(heading, statusLine, lastContact);

  const telemetry = device.telemetry || {};
  const detail = document.createElement("p");
  detail.className = "card-detail";
  if (Number.isInteger(telemetry.rssi)) {
    detail.textContent = `Sinal Wi-Fi: ${telemetry.rssi} dBm`;
  } else if (typeof telemetry.ip === "string") {
    detail.textContent = `IP informado: ${telemetry.ip}`;
  } else {
    detail.textContent = "Telemetria disponível após a próxima leitura.";
  }
  article.append(detail);

  const sensorHeading = document.createElement("h4");
  sensorHeading.className = "sensor-heading";
  sensorHeading.textContent = "HC-SR04 · ambiente de testes";
  const sensors = telemetry.sensors || {};
  const distance = sensors.hcSr04DistanceCm;
  const distanceLine = document.createElement("p");
  distanceLine.className = "card-value sensor-distance";
  distanceLine.textContent =
    typeof distance === "number" && Number.isFinite(distance)
      ? `${distance.toFixed(1)} cm`
      : "Aguardando leitura";
  const sensorState = document.createElement("p");
  sensorState.className = "card-detail";
  sensorState.textContent =
    typeof distance === "number"
      ? sensors.hcSr04Detected
        ? "Objeto dentro da faixa de detecção (até 30 cm)."
        : "Faixa livre (libera acima de 35 cm)."
      : "Conecte o sensor e aguarde a primeira amostra.";
  article.append(sensorHeading, distanceLine, sensorState);

  const eventCount = document.createElement("p");
  eventCount.className = "card-detail";
  eventCount.textContent = `Detecções registradas: ${device.sensorEventCount || 0}`;
  article.append(eventCount);
  const history = Array.isArray(device.sensorHistory) ? device.sensorHistory : [];
  if (history.length) {
    const chart = document.createElement("div");
    chart.className = "sensor-history";
    chart.setAttribute("aria-label", "Histórico de distâncias");
    for (const sample of history) {
      const bar = document.createElement("span");
      bar.className = sample.active ? "sensor-history-bar is-detected" : "sensor-history-bar";
      bar.style.height = `${Math.max(5, Math.min(48, (sample.distanceCm / 80) * 48))}px`;
      bar.title = `${sample.distanceCm} cm · ${formatDate(sample.at)}`;
      chart.append(bar);
    }
    article.append(chart);
  }
  const events = Array.isArray(device.sensorEvents) ? device.sensorEvents : [];
  for (const event of events.slice(0, 3)) {
    const eventItem = document.createElement("div");
    eventItem.className = "sensor-event";
    const eventTitle = document.createElement("strong");
    eventTitle.textContent =
      `Detecção #${event.id} · ${event.distanceCm} cm · ${formatDate(event.at)}`;
    const analysis = document.createElement("p");
    analysis.className = "card-detail";
    analysis.textContent = `${event.analysisStatus}: ${event.analysis}`;
    eventItem.append(eventTitle, analysis);
    article.append(eventItem);
  }
  return article;
}

async function refreshDashboard() {
  const updatedAt = document.querySelector("#dashboard-updated");
  try {
    const [health, deviceData] = await Promise.all([getHealth(), getDevices()]);
    const now = new Date();
    const serverStatus = document.querySelector("#server-status");
    serverStatus.dataset.status = health.status;
    serverStatus.lastElementChild.textContent = "Online";
    document.querySelector("#server-details").textContent =
      `Uptime: ${formatUptime(health.uptimeSeconds)} · API v${health.apiVersion}`;
    document.querySelector("#server-last-contact").textContent =
      `Último contato: ${formatDate(now)}`;
    document.querySelector("#ai-provider").textContent = providerLabel(health.provider);
    document.querySelector("#ai-model").textContent = `Modelo: ${health.model}`;
    const last = health.lastAiRequest;
    document.querySelector("#ai-last-test").textContent =
      last.status === "not_tested"
        ? "Nenhum teste real realizado"
        : `Última conversa: ${last.status === "success" ? "respondida" : "falhou"} · ${providerLabel(last.provider)} / ${last.model} · ${formatDate(last.at)}`;

    const deviceContent = document.querySelector("#device-content");
    devices = deviceData.devices;
    if (devices.length) {
      deviceContent.className = "cards device-list";
      deviceContent.replaceChildren(...devices.map(createDeviceCard));
      const select = document.querySelector("#control-device");
      const previous = select.value;
      select.replaceChildren(
        ...devices.map((device) => new Option(
          `${device.deviceId} · ${device.status === "connected" ? "conectado" : "desconectado"}`,
          device.deviceId
        ))
      );
      if (devices.some((device) => device.deviceId === previous)) select.value = previous;
      select.disabled = false;
      await refreshDeviceStatus();
    } else {
      deviceContent.className = "empty-state";
      deviceContent.textContent = "Aguardando primeiro contato autenticado do ESP32.";
      document.querySelector("#control-device").replaceChildren(
        new Option("Aguardando ESP32", "")
      );
      setControlsEnabled(false);
    }
    updatedAt.textContent = `Atualizado: ${formatDate(now)}`;
  } catch (error) {
    const serverStatus = document.querySelector("#server-status");
    serverStatus.dataset.status = "disconnected";
    serverStatus.lastElementChild.textContent = "Indisponível";
    document.querySelector("#server-details").textContent = error.message;
    updatedAt.textContent = `Falha na atualização: ${formatDate(new Date())}`;
  }
}

document.querySelector("#refresh-dashboard").addEventListener("click", refreshDashboard);
document.querySelector("#control-device").addEventListener("change", () => {
  oledSettingsDirty = false;
  controlDeviceId = "";
  void refreshDeviceStatus();
});
document.querySelector("#oled-welcome").addEventListener("input", () => {
  oledSettingsDirty = true;
});
document.querySelector("#oled-contrast").addEventListener("input", (event) => {
  oledSettingsDirty = true;
  document.querySelector("#oled-contrast-value").value = event.target.value;
});
document.querySelector("#save-oled").addEventListener("click", () => {
  void sendDeviceControl("screen", {
    active: oledActive,
    contrast: Number(document.querySelector("#oled-contrast").value),
    text: document.querySelector("#oled-welcome").value
  });
});
document.querySelector("#toggle-oled").addEventListener("click", () => {
  void sendDeviceControl("screen", {
    active: !oledActive,
    contrast: Number(document.querySelector("#oled-contrast").value),
    text: document.querySelector("#oled-welcome").value
  });
});
document.querySelector("#toggle-clock-mode").addEventListener("click", () => {
  void sendDeviceControl("clock-mode", { active: !clockModeActive });
});
document.querySelector("#toggle-microphone").addEventListener("click", () => {
  void sendDeviceControl("audio", { microphoneActive: !microphoneActive });
});
document.querySelector("#toggle-speaker").addEventListener("click", () => {
  void sendDeviceControl("audio", { speakerActive: !speakerActive });
});
document.querySelector("#restart-esp32").addEventListener("click", () => {
  if (window.confirm("Reiniciar o ESP32 agora? A conexão será interrompida brevemente.")) {
    void sendDeviceControl("reboot");
  }
});
document.querySelector("#save-esp-config").addEventListener("click", () => {
  void saveDeviceConfigForm();
});
document.querySelector("#load-esp-config").addEventListener("click", () => {
  void loadDeviceConfig();
});

void refreshDashboard();
refreshTimer = window.setInterval(refreshDashboard, 5000);
window.addEventListener("beforeunload", () => window.clearInterval(refreshTimer), { once: true });
