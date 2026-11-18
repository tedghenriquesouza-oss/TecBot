import {
  deleteConversation,
  getConversation,
  getConversations,
  sendChat
} from "./services/api.js";

const form = document.querySelector("#chat-form");
const input = document.querySelector("#message-input");
const submitButton = form.querySelector(".send-button");
const status = document.querySelector("#status");
const log = document.querySelector("#conversation-log");
const list = document.querySelector("#conversation-list");
const title = document.querySelector("#active-chat-title");

let activeConversationId = null;
let sending = false;

function setStatus(message, state = "normal") {
  status.textContent = message;
  status.dataset.state = state;
}

function appendMessage(role, text) {
  const article = document.createElement("article");
  article.className = `chat-message chat-message-${role}`;
  const label = document.createElement("span");
  label.className = "message-author";
  label.textContent = role === "user" ? "Você" : "TecBot";
  const content = document.createElement("p");
  content.textContent = text;
  article.append(label, content);
  log.append(article);
  log.scrollTop = log.scrollHeight;
}

function showWelcome() {
  const welcome = document.createElement("div");
  welcome.className = "chat-welcome";
  const heading = document.createElement("h2");
  heading.textContent = "Como posso ajudar?";
  const subtitle = document.createElement("p");
  subtitle.textContent = "Converse, explore ideias e tire suas dúvidas.";
  welcome.append(heading, subtitle);
  log.replaceChildren(welcome);
}

function resetChat() {
  if (sending) return;
  activeConversationId = null;
  title.textContent = "Como posso ajudar?";
  showWelcome();
  input.value = "";
  input.focus();
  void refreshConversationList();
}

async function refreshConversationList({ quiet = false } = {}) {
  try {
    const data = await getConversations();
    list.replaceChildren();
    if (!data.conversations.length) {
      const empty = document.createElement("p");
      empty.className = "conversation-list-empty";
      empty.textContent = "Suas conversas aparecerão aqui.";
      list.append(empty);
      if (!quiet) setStatus("Pronto.");
      return;
    }

    for (const conversation of data.conversations) {
      const item = document.createElement("div");
      item.className = "conversation-item";
      if (conversation.id === activeConversationId) item.classList.add("active");

      const openButton = document.createElement("button");
      openButton.type = "button";
      openButton.className = "conversation-open";
      openButton.textContent = conversation.title;
      openButton.title = conversation.title;
      openButton.setAttribute(
        "aria-current",
        conversation.id === activeConversationId ? "true" : "false"
      );
      openButton.addEventListener("click", () => void openConversation(conversation.id));

      const deleteButton = document.createElement("button");
      deleteButton.type = "button";
      deleteButton.className = "conversation-delete";
      deleteButton.textContent = "×";
      deleteButton.title = `Excluir ${conversation.title}`;
      deleteButton.setAttribute("aria-label", `Excluir conversa: ${conversation.title}`);
      deleteButton.addEventListener("click", () => void removeConversation(conversation.id));

      item.append(openButton, deleteButton);
      list.append(item);
    }
    if (!quiet) setStatus("Pronto.");
  } catch (error) {
    if (quiet) return;
    const message = document.createElement("p");
    message.className = "conversation-list-error";
    message.textContent = error.message;
    list.replaceChildren(message);
    setStatus("Histórico indisponível.", "error");
  }
}

async function openConversation(conversationId) {
  if (sending) return;
  try {
    const data = await getConversation(conversationId);
    activeConversationId = data.conversation.id;
    title.textContent = data.conversation.title;
    log.replaceChildren();
    if (data.messages.length) {
      for (const message of data.messages) {
        appendMessage(message.role, message.content);
      }
    } else {
      showWelcome();
    }
    setStatus("Conversa carregada.");
    await refreshConversationList();
  } catch (error) {
    setStatus(error.message, "error");
  }
}

async function removeConversation(conversationId) {
  if (sending) return;
  if (!window.confirm("Excluir esta conversa e seu histórico permanentemente?")) return;
  try {
    await deleteConversation(conversationId);
    if (activeConversationId === conversationId) {
      activeConversationId = null;
      title.textContent = "Como posso ajudar?";
      showWelcome();
    }
    await refreshConversationList();
    setStatus("Conversa excluída.");
  } catch (error) {
    setStatus(error.message, "error");
  }
}

async function submitMessage(text) {
  if (sending || !text.trim()) return;
  sending = true;
  submitButton.disabled = true;
  input.disabled = true;
  appendMessage("user", text.trim());
  setStatus("Pensando...", "pending");
  const controller = new AbortController();
  const isNewConversation = !activeConversationId;
  try {
    const result = await sendChat(
      text.trim(),
      activeConversationId,
      controller.signal
    );
    activeConversationId = result.conversationId;
    if (isNewConversation) title.textContent = text.trim().slice(0, 80);
    appendMessage("assistant", result.resposta);
    if (result.youtube?.url) appendMessage("assistant", result.youtube.url);
    setStatus("Pronto.");
    await refreshConversationList();
  } catch (error) {
    const message = document.createElement("p");
    message.className = "chat-error";
    message.textContent = error.message;
    log.append(message);
    log.scrollTop = log.scrollHeight;
    setStatus("Não foi possível concluir a mensagem.", "error");
  } finally {
    sending = false;
    submitButton.disabled = false;
    input.disabled = false;
    input.focus();
  }
}

form.addEventListener("submit", (event) => {
  event.preventDefault();
  const message = input.value.trim();
  if (!message) return;
  input.value = "";
  input.style.height = "auto";
  void submitMessage(message);
});

input.addEventListener("input", () => {
  input.style.height = "auto";
  input.style.height = `${Math.min(input.scrollHeight, 180)}px`;
});

input.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && !event.shiftKey) {
    event.preventDefault();
    form.requestSubmit();
  }
});

document.querySelector(".sidebar-new-chat").addEventListener("click", (event) => {
  event.preventDefault();
  resetChat();
});

showWelcome();
void refreshConversationList();
window.setInterval(() => {
  if (!sending && !document.hidden) {
    void refreshConversationList({ quiet: true });
  }
}, 10000);
