// SPDX-License-Identifier: MIT
// The editor's side of the local console build service.
//
// The editor does not build a ROM or a disc and does not patch one. It hands
// the project to `tools/rom_service/serve.mjs`, which runs the same pinned
// container build the gate runs, and collects what comes back. So what an
// author downloads is the checked build rather than something required to
// resemble it.
//
// Three shapes here are consequences of that decision rather than preferences.
//
// **Everything is fetched from a relative path.** `editor/index.html` declares
// `connect-src 'self'`, and it stays that way: the requests go to
// `/api/<console>`, which Vite proxies to the service in both `server` and
// `preview`. Widening the policy to name a localhost port would have been the
// easy way and would have spent a real security property on a development
// convenience.
//
// **A build is polled, not awaited.** It takes tens of seconds to minutes on
// the machine that measured it. A promise that settled at the end would give
// the surface nothing to say in between, which is how a wait becomes a hang.
//
// **A build produces files, not a file.** A Nintendo 64 ROM is one; a
// PlayStation disc is a `.bin` and the `.cue` that is its table of contents,
// and handing over the first without the second hands over something no
// burning program can read. So a finished build is a list, and every file
// keeps the name the service built it under, because a cue sheet names its own
// bin.

export type RomBuildState = "queued" | "building" | "done" | "failed";

// A console this service builds for, as the editor addresses it. The path
// segment is the service's own; `platform` is what the button says.
export interface RomTarget {
  readonly route: string;
  readonly platform: string;
  readonly image: string;
}

export const romTargets: Record<string, RomTarget> = {
  nintendo64: { route: "n64", platform: "Nintendo 64", image: "ROM" },
  playstation: { route: "playstation", platform: "PlayStation", image: "disc" }
};

// Every refusal the service can make, in the order its own table names them,
// followed by the two this side raises. Kept as a list rather than a string so
// that a code the editor has no words for is a type error here instead of an
// empty message in front of an author, and kept as a runtime list rather than
// only a type so that a test can hold it against the service's table, which is
// the only thing that can tell the two apart when the service grows a refusal.
export const romRefusalCodes = [
  "project_unreadable",
  "project_without_campaign",
  "character_style_not_served",
  "project_too_large_for_the_console",
  "campaign_id_not_an_identifier",
  "project_does_not_compile",
  "container_runtime_missing",
  "rom_build_failed",
  "rom_build_timed_out",
  "rom_build_queue_full",
  "rom_build_unknown",
  "character_art_is_not_one_combination",
  "request_from_another_site",
  "request_not_addressed_locally",
  // Raised where the ROM is asked for rather than where it is built: a build
  // the service is holding but has produced nothing for yet, and a service
  // that answered nothing at all.
  "rom_build_unfinished",
  "rom_service_unreachable"
] as const;

export type RomRefusalCode = typeof romRefusalCodes[number];

export interface RomRefusal {
  code: RomRefusalCode;
  message: string;
  detail?: string | undefined;
}

// One file a finished build produced, named as it will be downloaded.
export interface RomArtifact {
  name: string;
  bytes: number;
  md5: string;
}

export interface RomBuildStatus {
  id: string;
  state: RomBuildState;
  console: string;
  title: string;
  campaign: string;
  position: number;
  waiting: number;
  artifacts: RomArtifact[];
  log: string;
  error: RomRefusal | null;
}

// A finished build, as the surface receives it: every file, in the order the
// console produced them, each with the bytes and the name it is saved under.
export interface RomBuildResult {
  files: { name: string; bytes: Uint8Array }[];
  status: RomBuildStatus;
}

export interface RomServiceHealth {
  ready: boolean;
  code?: RomRefusalCode | undefined;
  message?: string | undefined;
  detail?: string | undefined;
  toolchainImagePresent?: boolean | undefined;
  compilerPresent?: boolean | undefined;
}

export class RomServiceError extends Error {
  readonly refusal: RomRefusal;

  constructor(refusal: RomRefusal) {
    super(refusal.message);
    this.refusal = refusal;
  }
}

// The one message this file composes rather than relaying. Every other refusal
// is the service's own words, and it matters that they are: the compiler's
// diagnostics and the toolchain's errors are the useful ones, and a wrapper
// that summarised them would be throwing away the only part an author can act
// on.
const unreachable: RomRefusal = {
  code: "rom_service_unreachable",
  message:
    "The local ROM build service is not running. Start it with " +
    "`node tools/rom_service/serve.mjs`."
};

export interface RomServiceOptions {
  // Injected so the unit suite can drive every state without a server, and so
  // the browser suite can assert what the editor says when there is none.
  fetch?: typeof globalThis.fetch;
  // How often to ask. Two seconds against a hundred-second build is fifty
  // requests, which is nothing, and it keeps the surface honest about moving.
  pollMilliseconds?: number;
  // Which console this instance builds for. One instance per console rather
  // than a console argument on every call: the surface holds one of these per
  // button, and a call that could be pointed at the wrong console is a call
  // that eventually is.
  target?: RomTarget;
}

export class RomService {
  readonly #fetch: typeof globalThis.fetch;
  readonly #pollMilliseconds: number;
  readonly #target: RomTarget;

  constructor(options: RomServiceOptions = {}) {
    this.#fetch = options.fetch ?? globalThis.fetch.bind(globalThis);
    this.#pollMilliseconds = options.pollMilliseconds ?? 2000;
    this.#target = options.target ?? romTargets.nintendo64!;
  }

  get target(): RomTarget {
    return this.#target;
  }

  async #json(path: string, init?: RequestInit): Promise<unknown> {
    let response: Response;
    try {
      response = await this.#fetch(path, init);
    } catch {
      // A connection that never opened is not a refusal the service made; it
      // is the service not being there, and saying so is more use than
      // relaying a network stack's word for it.
      throw new RomServiceError(unreachable);
    }
    // The proxy answers 404 with HTML when nothing is behind it, which is the
    // same situation by a different route.
    const contentType = response.headers.get("content-type") ?? "";
    if (!contentType.includes("application/json")) {
      throw new RomServiceError(unreachable);
    }
    const body = await response.json() as Record<string, unknown>;
    // The dev and preview proxies answer a service that is not running with a
    // 200 and this code, rather than the 502 the proxy would otherwise emit:
    // a failed request would put a console error on every page load in a
    // repository whose ordinary state is not to be running one. So the
    // absence arrives as a successful response carrying bad news, and has to
    // be recognised as bad news here.
    if (body.code === "rom_service_unreachable") {
      throw new RomServiceError({
        code: "rom_service_unreachable",
        message: (body.message as string) ?? unreachable.message
      });
    }
    if (!response.ok) {
      throw new RomServiceError({
        code: (body.code as RomRefusalCode) ?? "rom_build_failed",
        message: (body.message as string) ?? "The ROM build was refused.",
        detail: body.detail as string | undefined
      });
    }
    return body;
  }

  // Can this machine build at all? Asked before the button is offered, so that
  // the common failure is a control that says why it is unavailable rather
  // than one that fails after it is pressed.
  async health(): Promise<RomServiceHealth> {
    try {
      return await this.#json(
        `/api/${this.#target.route}/health`
      ) as RomServiceHealth;
    } catch (error) {
      if (error instanceof RomServiceError) {
        return {
          ready: false,
          code: error.refusal.code,
          message: error.refusal.message,
          detail: error.refusal.detail
        };
      }
      throw error;
    }
  }

  async start(projectJson: string): Promise<RomBuildStatus> {
    return await this.#json(`/api/${this.#target.route}/build`, {
      method: "POST",
      body: projectJson
    }) as RomBuildStatus;
  }

  async status(id: string): Promise<RomBuildStatus> {
    return await this.#json(
      `/api/${this.#target.route}/build/${encodeURIComponent(id)}`
    ) as RomBuildStatus;
  }

  async collect(id: string, name: string): Promise<Uint8Array> {
    let response: Response;
    try {
      response = await this.#fetch(
        `/api/${this.#target.route}/build/${
          encodeURIComponent(id)
        }/artifact/${encodeURIComponent(name)}`
      );
    } catch {
      throw new RomServiceError(unreachable);
    }
    if (!response.ok) {
      throw new RomServiceError({
        code: "rom_build_failed",
        message: `The finished ${this.#target.image} could not be collected.`,
        detail: name
      });
    }
    return new Uint8Array(await response.arrayBuffer());
  }

  // Start a build and follow it to its end, reporting every state change.
  //
  // Throws `RomServiceError` for a refusal, including a build that failed,
  // because from an author's chair "this project will not build" and "this
  // project is not one we serve" are the same kind of answer and differ only
  // in how much of it the toolchain wrote.
  async build(
    projectJson: string,
    onProgress: (status: RomBuildStatus) => void,
    sleep: (ms: number) => Promise<void> = (ms) =>
      new Promise((resolve) => setTimeout(resolve, ms))
  ): Promise<RomBuildResult> {
    let status = await this.start(projectJson);
    onProgress(status);
    while (status.state === "queued" || status.state === "building") {
      await sleep(this.#pollMilliseconds);
      status = await this.status(status.id);
      onProgress(status);
    }
    if (status.state === "failed") {
      throw new RomServiceError(status.error ?? {
        code: "rom_build_failed",
        message: `The ${this.#target.platform} build failed.`
      });
    }
    const files = [];
    for (const artifact of status.artifacts) {
      files.push({
        name: artifact.name,
        bytes: await this.collect(status.id, artifact.name)
      });
    }
    return { files, status };
  }
}
