async function requestJson(url, options = {}) {
  let response;
  try {
    response = await fetch(url, options);
  } catch (error) {
    if (error.name === "AbortError") throw error;
    throw new Error("Não foi possível conectar ao servidor do TecBot.");
  }

  let data;
  try {
    data = await response.json();
  } catch {
    throw new Error(
      response.ok
        ? "O servidor retornou uma resposta inválida."
        : `O servidor retornou um erro HTTP ${response.status}.`
    );
  }

  if (!response.ok) {
    throw new Error(data.erro || `O servidor retornou um erro HTTP ${response.status}.`);
  }
  return data;
}

export async function getSettings() {
  const data = await requestJson("/api/settings");
  if (!data.configuracoes) {
    throw new Error("O servidor não retornou as configurações.");
  }
  return data.configuracoes;
}

export async function saveSettings(settings) {
  const data = await requestJson("/api/settings", {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(settings)
  });
  if (!data.configuracoes) {
    throw new Error("O servidor não confirmou as configurações.");
  }
  return data.configuracoes;
}

export async function getProviderConfig() {
  const data = await requestJson("/api/provider-config");
  if (!data.provedores) {
    throw new Error("O servidor não retornou os provedores configurados.");
  }
  return data.provedores;
}

export async function saveProviderConfig(config) {
  const data = await requestJson("/api/provider-config", {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(config)
  });
  if (!data.provedores) {
    throw new Error("O servidor não confirmou os provedores.");
  }
  return data.provedores;
}

export async function resetSettings() {
  const data = await requestJson("/api/settings/reset", {
    method: "POST"
  });
  if (!data.configuracoes) {
    throw new Error("O servidor não confirmou a restauração.");
  }
  return data.configuracoes;
}

export function getModelCatalog(provider, refresh = false) {
  const params = new URLSearchParams({ provider });
  if (refresh) params.set("refresh", "true");
  return requestJson(`/api/models?${params}`);
}

export function getHealth() {
  return requestJson("/api/health");
}

export function getDevices() {
  return requestJson("/api/devices");
}

export function getDeviceTaskLogs({ before, limit = 100 } = {}) {
  const params = new URLSearchParams({ limit: String(limit) });
  if (before) params.set("before", String(before));
  return requestJson(`/api/devices/task-logs?${params}`);
}

export function getDeviceControlStatus(deviceId) {
  const params = new URLSearchParams({ deviceId });
  return requestJson(`/api/devices/control?${params}`);
}

export function controlDevice(payload) {
  return requestJson("/api/devices/control", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload)
  });
}

export function getDeviceConfig(deviceId) {
  const params = new URLSearchParams({ deviceId });
  return requestJson(`/api/devices/config?${params}`);
}

export function saveDeviceConfig(payload) {
  return requestJson("/api/devices/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload)
  });
}

export function getPublicDeviceConfig() {
  return requestJson("/api/device-config");
}

export function getVoiceStatus() {
  return requestJson("/api/voice/status");
}

export async function synthesizeLocalVoice(text, voiceRate, signal) {
  let response;
  try {
    response = await fetch("/api/voice/synthesize", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ text, voiceRate }),
      signal
    });
  } catch (error) {
    if (error.name === "AbortError") throw error;
    throw new Error("O serviço local de voz não está acessível.");
  }

  if (!response.ok) {
    let message = "O Piper local não conseguiu gerar o áudio.";
    try {
      const data = await response.json();
      if (typeof data.erro === "string") message = data.erro;
    } catch {
      if (response.status === 503) {
        message = "O Piper local ou o modelo de voz ainda não está instalado.";
      }
    }
    throw new Error(message);
  }

  const contentType = response.headers.get("content-type") || "";
  if (!contentType.toLowerCase().includes("audio/wav")) {
    throw new Error("O serviço local não retornou um arquivo WAV.");
  }
  const audio = await response.blob();
  if (!audio.size) {
    throw new Error("O Piper retornou um arquivo de áudio vazio.");
  }
  return audio;
}

export function getConversations() {
  return requestJson("/api/conversations");
}

export function getConversation(conversationId) {
  return requestJson(`/api/conversations/${encodeURIComponent(conversationId)}`);
}

export function deleteConversation(conversationId) {
  return requestJson(`/api/conversations/${encodeURIComponent(conversationId)}`, {
    method: "DELETE"
  });
}

export async function sendChat(message, conversationId, signal) {
  const body = { mensagem: message };
  if (conversationId) body.conversationId = conversationId;
  const data = await requestJson("/api/chat", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
    signal
  });
  if (typeof data.resposta !== "string" || !data.resposta.trim()) {
    throw new Error("A IA não retornou uma resposta válida.");
  }
  return data;
}
