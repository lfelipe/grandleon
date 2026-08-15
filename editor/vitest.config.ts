// SPDX-License-Identifier: MIT
import { defineConfig } from "vitest/config";
import vue from "@vitejs/plugin-vue";

export default defineConfig({
  plugins: [vue()],
  server: {
    fs: {
      allow: [".."]
    }
  },
  test: {
    environment: "happy-dom",
    include: ["src/**/*.test.ts"],
    setupFiles: ["src/test-setup.ts"],
    // Defaults to `!process.env.CI`, which means a committed `.only` runs one
    // test, skips the rest and exits 0 on every machine that is not a CI
    // server. There is no reason to commit one, so it is refused everywhere.
    allowOnly: false
  }
});
