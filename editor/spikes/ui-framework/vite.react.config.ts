// SPDX-License-Identifier: MIT
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  root: "react",
  base: "./",
  plugins: [react()],
  build: { outDir: "../dist/react", emptyOutDir: true, sourcemap: false }
});
