import {
  getProviderConfig,
  getModelCatalog,
  getSettings,
  getVoiceStatus,
  resetSettings,
  saveProviderConfig,
  saveSettings,
  synthesizeLocalVoice
} from "./services/api.js";

const form = document.querySelector("#settings-form");
const feedback = document.querySelector("#settings-feedback");
const providerInput = document.querySelector("#provider");
const modelInput = document.querySelector("#model");
const voiceInput = document.querySelector("#voice-id");
const catalogStatus = document.querySelector("#catalog-status");
const catalogList = document.querySelector("#model-catalog");
const customProviderList = document.querySelector("#custom-provider-list");
const providerConfigFeedback = document.querySelector("#provider-config-feedback");
let currentSettings = null;
let browserVoices = [];
let providerConfig = null;

function showFeedback(message, state = "normal") {
  feedback.textContent = message;
  feedback.dataset.state = state;
}

function updateRangeOutput(input, output, suffix = "") {
  output.value = `${Number(input.value).toFixed(1)}${suffix}`;
}

function fillVoiceOptions(selectedVoiceId = "") {
  const automatic = new Option("Automática", "");
  voiceInput.replaceChildren(automatic);
  for (const voice of browserVoices) {
    voiceInput.add(new Option(`${voice.name} (${voice.lang})`, voice.voiceURI));
  }
  voiceInput.value = browserVoices.some((voice) => voice.voiceURI === selectedVoiceId)
    ? selectedVoiceId
    : "";
}

function updateForm(settings) {
  currentSettings = settings;
  form.elements.assistantName.value = settings.assistantName;
  form.elements.instructions.value = settings.instructions;
  providerInput.value = settings.provider;
  modelInput.value = settings.model;
  form.elements.voiceLanguage.value = settings.voiceLanguage;
  form.elements.voiceRate.value = settings.voiceRate;
  form.elements.voicePitch.value = settings.voicePitch;
  form.elements.voiceVolume.value = settings.voiceVolume;
  form.elements.publicServerUrl.value = settings.publicServerUrl;
  updateRangeOutput(form.elements.voiceRate, document.querySelector("#voice-rate-value"), "x");
  updateRangeOutput(form.elements.voicePitch, document.querySelector("#voice-pitch-value"));
  updateRangeOutput(form.elements.voiceVolume, document.querySelector("#voice-volume-value"));
  fillVoiceOptions(settings.voiceId);
  document.querySelectorAll(".brand-name").forEach((element) => {
    element.textContent = settings.assistantName;
  });
  document.title = `${settings.assistantName} | Configurações`;
}

function updateProviderOptions(selectedProvider) {
  const builtIns = [
    ["gemini", "Google Gemini"],
    ["nvidia", "NVIDIA"]
  ];
  providerInput.replaceChildren(
    ...builtIns.map(([id, name]) => new Option(name, id)),
    ...providerConfig.customProviders.map(
      (provider) => new Option(provider.name, provider.id)
    )
  );
  providerInput.value = [...providerInput.options].some(
    (option) => option.value === selectedProvider
  ) ? selectedProvider : "gemini";
}

function renderProviderConfig() {
  customProviderList.replaceChildren();
  for (const [id, inputId] of [
    ["gemini", "gemini-api-key"],
    ["nvidia", "nvidia-api-key"]
  ]) {
    const provider = providerConfig.builtInProviders.find((item) => item.id === id);
    document.querySelector(`#${id}-key-status`).textContent = provider?.hasApiKey
      ? "Token configurado; o valor fica oculto."
      : "Token ainda não configurado.";
  }
  for (const provider of providerConfig.customProviders) {
    const card = document.createElement("section");
    card.className = "field field-full col-12";
    card.dataset.providerId = provider.id;
    const heading = document.createElement("h3");
    heading.textContent = provider.name;
    const fields = document.createElement("div");
    fields.className = "settings-grid row g-3";
    for (const [key, label, type, value, placeholder] of [
      ["name", "Nome", "text", provider.name, "Nome do provedor"],
      ["baseUrl", "URL base da API", "url", provider.baseUrl, "https://api.exemplo.com/v1"],
      ["apiKey", "Token", "password", "", provider.hasApiKey ? "Token não exibido" : "Token do provedor"]
    ]) {
      const wrapper = document.createElement("div");
      wrapper.className = "field col-12 col-md-6";
      const input = document.createElement("input");
      input.className = "form-control";
      input.type = type;
      input.maxLength = key === "apiKey" ? 4096 : key === "baseUrl" ? 500 : 200;
      input.autocomplete = type === "password" ? "new-password" : "off";
      input.placeholder = placeholder;
      input.value = value;
      input.dataset.providerField = key;
      const labelElement = document.createElement("label");
      labelElement.className = "form-label";
      labelElement.textContent = label;
      wrapper.append(labelElement, input);
      fields.append(wrapper);
    }
    const idNote = document.createElement("small");
    idNote.textContent =
      `Identificador: ${provider.id}. Escolha o modelo na seção Assistente.`;
    const removeButton = document.createElement("button");
    removeButton.className = "button btn button-secondary btn-secondary";
    removeButton.type = "button";
    removeButton.textContent = "Remover provedor";
    removeButton.addEventListener("click", () => {
      card.remove();
      providerConfig.customProviders = providerConfig.customProviders.filter(
        (item) => item.id !== provider.id
      );
      updateProviderOptions(providerInput.value);
    });
    card.append(heading, fields, idNote, removeButton);
    customProviderList.append(card);
  }
}

function fillModelOptions(models, configuredModel) {
  const options = document.querySelector("#model-options");
  options.replaceChildren();
  for (const model of models.filter((item) => item.chatSupport !== "unsupported")) {
    const option = document.createElement("option");
    option.value = model.id;
    option.label = model.displayName;
    options.append(option);
  }
  if (configuredModel && !models.some((item) => item.id === configuredModel)) {
    const option = document.createElement("option");
    option.value = configuredModel;
    option.label = `${configuredModel} (configurado)`;
    options.append(option);
  }
  document.querySelector("#model-configured-note").textContent =
    "Informe um ID de modelo ou escolha uma sugestão do catálogo.";
}

async function loadModelCatalog(refresh = false) {
  catalogStatus.textContent = "Consultando modelos disponíveis...";
  catalogStatus.dataset.state = "pending";
  catalogList.replaceChildren();
  try {
    const data = await getModelCatalog(providerInput.value, refresh);
    fillModelOptions(data.availableModels, currentSettings?.model);
    for (const model of data.availableModels) {
      const item = document.createElement("li");
      const name = document.createElement("span");
      name.textContent = model.displayName;
      const capability = document.createElement("small");
      capability.textContent =
        model.chatSupport === "supported"
          ? "Geração de texto"
          : model.chatSupport === "unsupported"
            ? "Sem suporte a chat"
            : "Capacidades não informadas";
      item.append(name, capability);
      catalogList.append(item);
    }
    catalogStatus.textContent = data.availableModels.length
      ? `Modelos listados pela API (${data.cached ? "cache" : "atualizado"}).`
      : providerConfig.customProviders.some(
          (provider) => provider.id === providerInput.value
        )
        ? "Informe manualmente o ID do modelo configurado neste provedor."
        : "O provedor não retornou modelos disponíveis.";
    catalogStatus.dataset.state = "success";
  } catch (error) {
    fillModelOptions([], currentSettings?.model);
    catalogStatus.textContent = error.message;
    catalogStatus.dataset.state = "error";
  }
}

function selectedVoice(language) {
  return (
    browserVoices.find((voice) => voice.voiceURI === voiceInput.value) ||
    browserVoices.find((voice) => voice.lang.toLowerCase() === language.toLowerCase()) ||
    browserVoices.find((voice) =>
      voice.lang.toLowerCase().startsWith(language.slice(0, 2).toLowerCase())
    ) ||
    browserVoices.find((voice) => voice.default) ||
    null
  );
}

async function previewVoice() {
  const text = `Olá! Esta é uma prévia da voz do ${currentSettings?.assistantName || "TecBot"}.`;
  const rate = Number(form.elements.voiceRate.value);
  const volume = Number(form.elements.voiceVolume.value);
  showFeedback("Preparando a prévia...", "pending");
  try {
    const blob = await synthesizeLocalVoice(text, rate);
    const objectUrl = URL.createObjectURL(blob);
    const audio = new Audio(objectUrl);
    audio.volume = volume;
    audio.onended = () => URL.revokeObjectURL(objectUrl);
    audio.onerror = () => {
      URL.revokeObjectURL(objectUrl);
      showFeedback("Não foi possível reproduzir o áudio.", "error");
    };
    await audio.play();
    showFeedback("Reproduzindo prévia do Piper local.", "success");
  } catch (error) {
    if (!("speechSynthesis" in window)) {
      showFeedback(error.message, "error");
      return;
    }
    const utterance = new SpeechSynthesisUtterance(text);
    utterance.lang = form.elements.voiceLanguage.value;
    utterance.rate = rate;
    utterance.pitch = Number(form.elements.voicePitch.value);
    utterance.volume = volume;
    const voice = selectedVoice(utterance.lang);
    if (voice) utterance.voice = voice;
    utterance.onend = () => showFeedback("Prévia reproduzida pelo navegador.", "success");
    utterance.onerror = () => showFeedback("Não foi possível reproduzir a prévia.", "error");
    window.speechSynthesis.speak(utterance);
  }
}

providerInput.addEventListener("change", () => void loadModelCatalog());
document.querySelector("#refresh-models").addEventListener("click", () => {
  void loadModelCatalog(true);
});

document.querySelector("#add-custom-provider").addEventListener("click", () => {
  const existingIds = new Set(providerConfig.customProviders.map((item) => item.id));
  let suffix = providerConfig.customProviders.length + 1;
  while (existingIds.has(`provedor-${suffix}`)) suffix++;
  providerConfig.customProviders.push({
    id: `provedor-${suffix}`,
    name: `Provedor ${suffix}`,
    baseUrl: "https://api.exemplo.com/v1",
    hasApiKey: false
  });
  renderProviderConfig();
  updateProviderOptions(providerInput.value);
});

document.querySelector("#save-provider-config").addEventListener("click", async () => {
  providerConfigFeedback.textContent = "Salvando tokens...";
  providerConfigFeedback.dataset.state = "pending";
  const customProviders = [...customProviderList.querySelectorAll("[data-provider-id]")]
    .map((card) => {
      const fields = Object.fromEntries(
        [...card.querySelectorAll("[data-provider-field]")].map((input) => [
          input.dataset.providerField,
          input.value.trim()
        ])
      );
      return { id: card.dataset.providerId, ...fields };
    });
  try {
    providerConfig = await saveProviderConfig({
      geminiApiKey: document.querySelector("#gemini-api-key").value,
      nvidiaApiKey: document.querySelector("#nvidia-api-key").value,
      customProviders
    });
    document.querySelector("#gemini-api-key").value = "";
    document.querySelector("#nvidia-api-key").value = "";
    renderProviderConfig();
    updateProviderOptions(providerInput.value);
    providerConfigFeedback.textContent = "Tokens e provedores salvos localmente no backend.";
    providerConfigFeedback.dataset.state = "success";
    void loadModelCatalog();
  } catch (error) {
    providerConfigFeedback.textContent = error.message;
    providerConfigFeedback.dataset.state = "error";
  }
});

for (const [inputId, outputId, suffix] of [
  ["voice-rate", "voice-rate-value", "x"],
  ["voice-pitch", "voice-pitch-value", ""],
  ["voice-volume", "voice-volume-value", ""]
]) {
  const input = document.querySelector(`#${inputId}`);
  input.addEventListener("input", () =>
    updateRangeOutput(input, document.querySelector(`#${outputId}`), suffix)
  );
}

form.addEventListener("submit", async (event) => {
  event.preventDefault();
  showFeedback("Salvando...", "pending");
  const nextSettings = {
    assistantName: form.elements.assistantName.value,
    instructions: form.elements.instructions.value,
    provider: providerInput.value,
    model: modelInput.value.trim(),
    voiceRate: Number(form.elements.voiceRate.value),
    voicePitch: Number(form.elements.voicePitch.value),
    voiceVolume: Number(form.elements.voiceVolume.value),
    voiceId: voiceInput.value,
    voiceLanguage: form.elements.voiceLanguage.value,
    publicServerUrl: form.elements.publicServerUrl.value
  };
  try {
    const saved = await saveSettings(nextSettings);
    updateForm(saved);
    showFeedback("Configurações salvas.", "success");
    void loadModelCatalog();
  } catch (error) {
    showFeedback(error.message, "error");
  }
});

document.querySelector("#reset-settings").addEventListener("click", async () => {
  showFeedback("Restaurando valores padrão...", "pending");
  try {
    const defaults = await resetSettings();
    updateForm(defaults);
    showFeedback("Valores padrão restaurados.", "success");
    void loadModelCatalog();
  } catch (error) {
    showFeedback(error.message, "error");
  }
});

document.querySelector("#preview-voice").addEventListener("click", () => {
  void previewVoice();
});

if ("speechSynthesis" in window) {
  const loadVoices = () => {
    browserVoices = window.speechSynthesis.getVoices();
    fillVoiceOptions(currentSettings?.voiceId);
  };
  window.speechSynthesis.onvoiceschanged = loadVoices;
  loadVoices();
}

getVoiceStatus()
  .then((voiceStatus) => {
    if (!voiceStatus.available) {
      showFeedback("Piper local indisponível. A prévia usará a voz do navegador.", "normal");
    }
  })
  .catch((error) => showFeedback(error.message, "error"));

Promise.all([getSettings(), getProviderConfig()])
  .then(([settings, configs]) => {
    providerConfig = configs;
    updateProviderOptions(settings.provider);
    renderProviderConfig();
    updateForm(settings);
    return loadModelCatalog();
  })
  .catch((error) => showFeedback(error.message, "error"));
