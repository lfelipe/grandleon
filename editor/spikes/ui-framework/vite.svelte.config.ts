// SPDX-License-Identifier: MIT
import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";

export default defineConfig({
  root: "svelte",
  base: "./",
  plugins: [svelte()],
  build: { outDir: "../dist/svelte", emptyOutDir: true, sourcemap: false }
});
