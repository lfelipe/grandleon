// SPDX-License-Identifier: MIT
import { hostname } from "node:os";
import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";

// The name this server prints at startup. It advertises an address; it does
// not decide one.
//
// Nothing below stamps it into what the browser is served. That distinction is
// the whole of a fault this configuration used to carry: `server.origin` was
// pinned to a name, every module URL was stamped with it, and the URL
// `new Worker(new URL(...))` builds was therefore cross-origin for anybody who
// had reached the editor by any *other* name. The browser refuses such a
// worker, the refusal throws out of the application's mounted hook, and the
// editor comes up with no stored draft, no engine and no ROM button — while
// reporting no problems, because the thing that reports problems is what
// failed to start.
//
// One name cannot be right here. This machine answers to at least three
// (`localhost`, its own hostname, and that name with `.local` on the end), a
// person may reach it by any of them, and which one they used is a fact only
// the request knows. So the request decides, and this is a label.
const machineHostname = hostname();
const editorHostname =
  process.env.GRANDLEON_EDITOR_HOSTNAME ?? machineHostname;

// `--port` on the command line, read here rather than left to Vite alone.
//
// Vite merges its own command-line options over this file after it has been
// evaluated, so `--port` moves the listening socket whatever this file says.
// Two things below are derived from the port instead of read back from it: the
// port the HMR client dials, and the line printed at startup. Left to Vite,
// both keep the value here while the socket moves, which prints a dead address
// and points HMR at nothing.
//
// The *host* half of each is deliberately not derived from anything, and the
// comment above `editorHostname` says why.
function portFromCommandLine(argv: readonly string[]): number | null {
  let stated: string | null = null;
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index]!;
    if (argument === "--port") stated = argv[index + 1] ?? "";
    else if (argument.startsWith("--port=")) stated = argument.slice(7);
  }
  if (stated === null) return null;
  const port = Number(stated);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error(
      `--port needs a port number, not '${stated}'. ` +
      "GRANDLEON_EDITOR_PORT sets the same thing from the environment."
    );
  }
  return port;
}

const editorPort =
  portFromCommandLine(process.argv) ??
  Number(process.env.GRANDLEON_EDITOR_PORT ?? "5173");
if (!Number.isInteger(editorPort) || editorPort < 1 || editorPort > 65535) {
  throw new Error(
    `GRANDLEON_EDITOR_PORT needs a port number, not ` +
    `'${process.env.GRANDLEON_EDITOR_PORT}'.`
  );
}

// Which names this server will answer to, beyond loopback.
//
// Vite refuses a request whose `Host` is not on this list and answers 403 with
// a sentence naming the host it refused. That is worth keeping — it is what
// stops a page on another site resolving to this machine — but the guess below
// it is only a guess: `.local` is mDNS, and a network whose domain is anything
// else hands out a name this list has never heard of. On a `.lan` network the
// machine's own FQDN is refused, and the symptom is an editor that "does not
// show" from the second machine it exists to be reachable from.
//
// So the guess stays and a way past it is added, spelled the way the ROM
// service beside it spells the same idea (`--allow-host`, or
// `GRANDLEON_ROM_SERVICE_ALLOWED_HOSTS`). One name or a comma-separated list.
const allowedEditorHosts = [machineHostname, `${machineHostname}.local`];
if (!allowedEditorHosts.includes(editorHostname)) {
  allowedEditorHosts.push(editorHostname);
}
for (const named of (process.env.GRANDLEON_EDITOR_ALLOWED_HOSTS ?? "")
  .split(",")
  .map((entry) => entry.trim())
  .filter((entry) => entry !== "")) {
  if (!allowedEditorHosts.includes(named)) allowedEditorHosts.push(named);
}

// The local ROM build service, proxied rather than fetched across origins.
//
// `index.html` declares `connect-src 'self'` and this is what keeps it that
// way: the editor asks for `/api/n64/...`, which is same-origin, and the
// request is forwarded here. Naming a localhost port in the policy instead
// would have been one line, and would have spent a real security property to
// save this one.
//
// Declared for `preview` as well as `server`, and that is not symmetry for its
// own sake: `vite preview` does not honour `server.proxy`, and the Playwright
// suite runs against `vite preview`. A dev-only proxy would be invisible to
// the only gate that can observe a CSP violation at all.
// This dev server binds every interface, so the proxy below is reachable from
// the network and forwards `/api/n64` with the browser's own `Host` intact.
// What keeps that from publishing a container build service to the LAN is the
// service itself: it refuses any `Host` that is not loopback, by name, and a
// peer reaching the editor over the network is told the ROM build is
// unavailable rather than quietly given one. See `tools/rom_service/README.md`,
// "Who is allowed to ask". The check belongs there because this proxy cannot
// tell a rebound name from a real one and the service can.
//
// A service that is not running answers as *data*, not as a failed request.
//
// Left alone, the proxy replies 502 when nothing is listening, and Chromium
// writes "Failed to load resource" to the console for it. Not running a build
// service is the ordinary state of this repository, the browser gate never
// having one, so that would put a console error on every page load, and several
// existing tests quite rightly assert there are none. Answering 200 with a
// body that says the service is absent is also simply more truthful: the
// editor asked whether a ROM can be built here, and "no" is an answer rather
// than a failure.
const romServiceAbsent = {
  ready: false,
  code: "rom_service_unreachable",
  message:
    "The local ROM build service is not running. Start it with " +
    "`node tools/rom_service/serve.mjs`."
};

const romServiceProxy = {
  "/api/n64": {
    // Loopback by default, because the ordinary way to run both is two
    // processes on one machine. `GRANDLEON_ROM_SERVICE_HOST` is for the case
    // where they are not: under `compose.yaml` the service is a container of
    // its own, reached by service name, and 127.0.0.1 there is this container
    // rather than that one.
    target: `http://${
      process.env.GRANDLEON_ROM_SERVICE_HOST ?? "127.0.0.1"
    }:${
      process.env.GRANDLEON_ROM_SERVICE_PORT ?? "4699"
    }`,
    changeOrigin: false,
    configure: (proxy: {
      on: (event: string, handler: (
        error: Error,
        request: unknown,
        response: {
          writableEnded?: boolean;
          writeHead: (status: number, headers: Record<string, string>) => void;
          end: (body: string) => void;
        }
      ) => void) => void;
    }) => {
      proxy.on("error", (_error, _request, response) => {
        if (response.writableEnded) return;
        const body = JSON.stringify(romServiceAbsent);
        response.writeHead(200, { "content-type": "application/json" });
        response.end(body);
      });
    }
  }
};

// `index.html` carries the policy a deployment is served under, and the dev
// server cannot be served under it.
//
// Vite in development hands every stylesheet to the page as an inline
// `<style>` element, so `style-src 'self'` blocks the lot: the editor is
// fully functional and rendered in the browser's default fonts, buttons and
// layout, with a console full of refusals. Adding `'unsafe-inline'` to the
// document's own policy would fix that everywhere, which is the trade this
// declines: the policy in `index.html` is the one real people are served, and
// `editor/scripts/deployment-smoke.mjs` checks the built copy of it.
//
// So the relaxation lives here, applied to the served HTML and never to a
// build. `apply: "serve"` is what makes that structural rather than careful:
// the hook is not part of a build at all, so `editor/dist/index.html` carries
// the policy exactly as written.
//
// One directive and one keyword, because the point is a styled page and not a
// convenient policy. `editor/scripts/dev-server-smoke.mjs` fails if the served
// page is unstyled or if the browser refuses anything else.
const developmentStylePolicy = (policy: string): string => {
  const directives = policy.split(";").map((directive) => directive.trim());
  const index = directives.findIndex(
    (directive) => directive === "style-src" || directive.startsWith("style-src ")
  );
  if (index === -1) {
    throw new Error("index.html's Content-Security-Policy has no style-src");
  }
  directives[index] = `${directives[index]!} 'unsafe-inline'`;
  return directives.join("; ");
};

const developmentContentSecurityPolicy = {
  name: "grandleon-editor-development-csp",
  apply: "serve" as const,
  transformIndexHtml: {
    order: "pre" as const,
    handler(html: string): string {
      const tag = /(<meta http-equiv="Content-Security-Policy"[\s\S]*?content=")([^"]*)(")/;
      if (!tag.test(html)) {
        throw new Error("index.html declares no Content-Security-Policy meta tag");
      }
      return html.replace(
        tag,
        (_match, before: string, policy: string, after: string) =>
          `${before}${developmentStylePolicy(policy)}${after}`
      );
    }
  }
};

export default defineConfig({
  base: process.env.GRANDLEON_EDITOR_BASE ?? "/",
  plugins: [
    vue(),
    developmentContentSecurityPolicy,
    {
      name: "grandleon-editor-network-url",
      configureServer(server) {
        server.httpServer?.once("listening", () => {
          // This machine's own name, because that is the address somebody
          // editing from a second computer types, and Vite's own lines name
          // only loopback and the raw interface addresses.
          //
          // Whether a ROM can be built from here is deliberately not claimed:
          // it depends on which names the ROM service was started with, which
          // this server has no way to know. The editor asks the service and
          // labels its own button with the answer.
          server.config.logger.info(
            `  ➜  Editor:  http://${editorHostname}:${editorPort}/`
          );
        });
      }
    }
  ],
  server: {
    host: "0.0.0.0",
    port: editorPort,
    strictPort: true,
    // No `origin` and no `hmr.host`. Both would pin a name, and the name that
    // is right is the one in the address bar, which only the request knows.
    // Left unset, Vite serves module URLs relative to the request and the HMR
    // client dials the page's own host, so `localhost`, `cruncher` and
    // `cruncher.local` each work as themselves. `connect-src 'self'` in the
    // page's policy then holds for all three, where a pinned name violated it
    // for two.
    //
    // The port is still pinned, and has to be: it is read from the command
    // line above precisely so the HMR client dials the socket that moved.
    hmr: { port: editorPort },
    allowedHosts: allowedEditorHosts,
    proxy: romServiceProxy
  },
  preview: {
    host: "0.0.0.0",
    port: editorPort,
    strictPort: true,
    allowedHosts: allowedEditorHosts,
    proxy: romServiceProxy
  },
  build: {
    outDir: "dist",
    sourcemap: true
  }
});
