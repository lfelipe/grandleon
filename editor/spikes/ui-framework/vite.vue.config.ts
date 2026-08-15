// SPDX-License-Identifier: MIT
import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";

export default defineConfig({
  root: "vue",
  base: "./",
  plugins: [vue()],
  build: { outDir: "../dist/vue", emptyOutDir: true, sourcemap: false }
});
