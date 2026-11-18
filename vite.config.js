import { defineConfig } from "vite";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const rootDirectory = fileURLToPath(new URL(".", import.meta.url));

export default defineConfig({
  build: {
    rollupOptions: {
      input: {
        chat: resolve(rootDirectory, "index.html"),
        dashboard: resolve(rootDirectory, "dashboard.html"),
        espLogs: resolve(rootDirectory, "esp-logs.html"),
        settings: resolve(rootDirectory, "settings.html")
      }
    }
  },
  server: {
    host: "0.0.0.0",
    proxy: {
      "/api": {
        target: "http://localhost:3000",
        changeOrigin: true
      }
    }
  }
});