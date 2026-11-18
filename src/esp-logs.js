import { getDeviceTaskLogs } from "./services/api.js";

const list = document.querySelector("#esp-log-list");
const status = document.querySelector("#esp-logs-status");
const loadOlderButton = document.querySelector("#load-older-logs");

let oldestLogId = null;
let loading = false;

function formatDate(value) {
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? value : date.toLocaleString("pt-BR");
}

function appendDetail(container, label, value) {
  if (value === undefined || value === null || value === "") return;
  const line = document.createElement("p");
  const name = document.createElement("strong");
  name.textContent = `${label}: `;
  const text = document.createElement("span");
  text.textContent = String(value);
  line.append(name, text);
  container.append(line);
}

function createLogEntry(log) {
  const article = document.createElement("article");
  article.className = "esp-log-entry";
  article.dataset.type = log.tipo;
  article.dataset.id = String(log.id);

  const header = document.createElement("div");
  header.className = "esp-log-header";
  const heading = document.createElement("h2");
  heading.textContent = log.titulo;
  const badge = document.createElement("span");
  badge.className = "esp-log-badge";
  badge.textContent = {
    question: "Pergunta",
    speech: "Áudio",
    connection: "Conexão",
    sensor: "Sensor",
    control: "Comando",
    config: "Configuração"
  }[log.tipo] || "Atividade";
  header.append(heading, badge);

  const metadata = document.createElement("p");
  metadata.className = "esp-log-meta";
  metadata.textContent = `${formatDate(log.criadoEm)} · ${log.deviceId}`;

  const details = document.createElement("div");
  details.className = "esp-log-details";
  if (log.tipo === "question") {
    appendDetail(
      details,
      "Você falou",
      log.detalhes.pergunta ?? log.detalhes.transcricao
    );
    appendDetail(details, "Resposta do agente", log.detalhes.resposta);
    appendDetail(details, "Erro", log.detalhes.erro);
  } else if (log.tipo === "sensor") {
    appendDetail(details, "Distância", `${log.detalhes.distanciaCm} cm`);
    appendDetail(details, "Limiar de detecção", `${log.detalhes.limiarCm} cm`);
  } else if (log.tipo === "control") {
    appendDetail(details, "Tela OLED", log.detalhes.telaAtiva === undefined
      ? undefined
      : log.detalhes.telaAtiva ? "ligada" : "desligada");
    appendDetail(details, "Contraste", log.detalhes.contraste);
    appendDetail(details, "Texto inicial", log.detalhes.textoInicial);
    appendDetail(details, "Modo horário", log.detalhes.ativo === undefined
      ? undefined
      : log.detalhes.ativo ? "ativado" : "desativado");
  } else if (log.tipo === "config") {
    appendDetail(details, "Campos alterados", log.detalhes.camposAlterados?.join(", "));
  } else if (log.tipo === "speech") {
    appendDetail(details, "Mensagem", log.detalhes.mensagemId);
    appendDetail(details, "Erro", log.detalhes.erro);
  }

  article.append(header, metadata, details);
  return article;
}

async function loadLogs({ older = false } = {}) {
  if (loading) return;
  loading = true;
  status.dataset.state = "normal";
  status.textContent = older ? "Carregando registros anteriores..." : "Atualizando registros...";
  loadOlderButton.disabled = true;
  try {
    const response = await getDeviceTaskLogs({
      before: older ? oldestLogId : undefined,
      limit: 100
    });
    const entries = response.logs.map(createLogEntry);
    if (older) {
      list.append(...entries);
    } else {
      list.replaceChildren(...entries);
    }
    oldestLogId = response.logs.length
      ? response.logs[response.logs.length - 1].id
      : null;
    loadOlderButton.dataset.hasOlder = String(response.hasMore);
    loadOlderButton.hidden = !response.hasMore;
    status.textContent = response.logs.length || older
      ? `${list.childElementCount} registro(s) carregado(s).`
      : "Ainda não há registros do ESP32. Perguntas e atividades aparecerão aqui.";
  } catch (error) {
    status.textContent = error.message;
    status.dataset.state = "error";
  } finally {
    loading = false;
    loadOlderButton.disabled = false;
  }
}

async function refreshNewLogs() {
  if (loading || document.hidden) return;
  try {
    const response = await getDeviceTaskLogs({ limit: 100 });
    const knownIds = new Set(
      Array.from(list.children, (entry) => entry.dataset.id)
    );
    const newEntries = response.logs
      .filter((entry) => !knownIds.has(String(entry.id)))
      .map(createLogEntry)
      .reverse();
    for (const entry of newEntries) {
      list.prepend(entry);
    }
    if (response.logs.length && oldestLogId === null) {
      oldestLogId = response.logs[response.logs.length - 1].id;
    }
    loadOlderButton.hidden = !response.hasMore && !loadOlderButton.dataset.hasOlder;
    loadOlderButton.dataset.hasOlder = String(response.hasMore);
    status.dataset.state = "normal";
    status.textContent = list.childElementCount
      ? `${list.childElementCount} registro(s) carregado(s).`
      : "Ainda não há registros do ESP32. Perguntas e atividades aparecerão aqui.";
  } catch (error) {
    status.textContent = error.message;
    status.dataset.state = "error";
  }
}

document.querySelector("#refresh-esp-logs").addEventListener("click", () => {
  status.dataset.state = "normal";
  loadOlderButton.dataset.hasOlder = "";
  void loadLogs();
});
loadOlderButton.addEventListener("click", () => void loadLogs({ older: true }));

void loadLogs();
window.setInterval(() => void refreshNewLogs(), 5000);
