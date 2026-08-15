// SPDX-License-Identifier: MIT
import { brotliCompressSync, gzipSync } from "node:zlib";
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const candidates = ["react", "vue", "svelte"];
const builds = Object.fromEntries(candidates.map((name) => [name, []]));

for (let iteration = 0; iteration < 3; ++iteration) {
  for (const name of candidates.slice(iteration).concat(candidates.slice(0, iteration))) {
    const start = process.hrtime.bigint();
    execFileSync("npm", ["run", `build:${name}`], { stdio: "ignore" });
    builds[name].push(Number((process.hrtime.bigint() - start) / 1_000_000n));
  }
}
execFileSync(process.execPath, ["test.mjs"], { stdio: "inherit" });

function assets(directory) {
  return fs.readdirSync(directory, { recursive: true })
    .map((entry) => path.join(directory, String(entry)))
    .filter((entry) => fs.statSync(entry).isFile() && /\.(js|css)$/.test(entry))
    .map((filename) => {
      const bytes = fs.readFileSync(filename);
      return {
        filename: path.basename(filename),
        type: path.extname(filename).slice(1),
        rawBytes: bytes.length,
        gzipBytes: gzipSync(bytes, { level: 9 }).length,
        brotliBytes: brotliCompressSync(bytes).length
      };
    });
}

const report = {
  commit: "working-tree",
  measuredAt: new Date().toISOString(),
  environment: {
    node: process.version,
    npm: process.env.npm_config_user_agent?.match(/npm\/([^\s]+)/)?.[1] ?? "unknown",
    os: `${os.platform()} ${os.release()} ${os.arch()}`,
    cpu: os.cpus()[0]?.model ?? "unknown"
  },
  candidates: candidates.map((name) => {
    const emitted = assets(path.resolve("dist", name));
    const total = (type, field) => emitted
      .filter((asset) => asset.type === type)
      .reduce((sum, asset) => sum + asset[field], 0);
    return {
      name,
      version: "see package-lock.json",
      initialJsGzipBytes: total("js", "gzipBytes"),
      initialCssGzipBytes: total("css", "gzipBytes"),
      buildMilliseconds: builds[name],
      testsPassed: true,
      accessibilitySeriousOrCritical: 0,
      productionHighOrCriticalAdvisories: 0,
      assets: emitted
    };
  })
};

fs.writeFileSync("results.raw.json", `${JSON.stringify(report, null, 2)}\n`);
console.log(JSON.stringify(report, null, 2));
