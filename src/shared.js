import { getPublicDeviceConfig } from "./services/api.js";

const root = document.documentElement;
const themeButtons = [...document.querySelectorAll("[data-theme-toggle]")];

function applyTheme(theme) {
  const selectedTheme = theme === "light" ? "light" : "dark";
  root.dataset.theme = selectedTheme;
  document
    .querySelector('meta[name="theme-color"]')
    .setAttribute("content", selectedTheme === "light" ? "#ffffff" : "#212121");
  for (const button of themeButtons) {
    const nextTheme = selectedTheme === "dark" ? "light" : "dark";
    button.setAttribute(
      "aria-label",
      `Ativar tema ${nextTheme === "light" ? "claro" : "escuro"}`
    );
    button.querySelector(".theme-label").textContent =
      `Tema ${nextTheme === "light" ? "claro" : "escuro"}`;
  }
  try {
    localStorage.setItem("tecbot-theme", selectedTheme);
  } catch {
    return;
  }
}

let initialTheme = "dark";
try {
  initialTheme = localStorage.getItem("tecbot-theme") || initialTheme;
} catch {
  initialTheme = "dark";
}
applyTheme(initialTheme);

for (const button of themeButtons) {
  button.addEventListener("click", () => {
    applyTheme(root.dataset.theme === "dark" ? "light" : "dark");
  });
}

getPublicDeviceConfig()
  .then((config) => {
    if (typeof config.assistantName !== "string" || !config.assistantName.trim()) {
      return;
    }
    document.querySelectorAll(".brand-name").forEach((element) => {
      element.textContent = config.assistantName;
    });
    document.querySelectorAll(".brand").forEach((element) => {
      element.setAttribute("aria-label", `${config.assistantName}, início`);
    });
    if (document.body.dataset.page === "chat") {
      document.title = `${config.assistantName} | Chat`;
    }
  })
  .catch(() => {
    console.error("Não foi possível carregar a configuração pública do TecBot.");
  });
