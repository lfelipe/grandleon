// SPDX-License-Identifier: MIT
import type { SourceAnalysis } from "./source-analysis";
import type {
  AnalysisWorkerRequest,
  AnalysisWorkerResponse
} from "./analysis-worker-protocol";

export interface AnalysisWorkerPort {
  postMessage(message: AnalysisWorkerRequest): void;
  addEventListener(
    type: "message" | "error" | "messageerror",
    listener: (event: MessageEvent<AnalysisWorkerResponse> | ErrorEvent) => void
  ): void;
  removeEventListener(
    type: "message" | "error" | "messageerror",
    listener: (event: MessageEvent<AnalysisWorkerResponse> | ErrorEvent) => void
  ): void;
  terminate(): void;
}

// A worker that fails to load never replies, and a reply the structured clone
// cannot deliver never arrives either. Both must settle every caller: an
// unsettled analyze() leaves the toolbar disabled with edits pending.
const defaultTimeoutMs = 10_000;

export class AnalysisWorkerClient {
  #nextId = 1;
  #failure: Error | undefined;
  readonly #timeoutMs: number;
  readonly #pending = new Map<number, {
    resolve: (analysis: SourceAnalysis) => void;
    reject: (error: Error) => void;
    timer: ReturnType<typeof setTimeout>;
  }>();

  constructor(
    readonly worker: AnalysisWorkerPort,
    options?: { readonly timeoutMs?: number }
  ) {
    this.#timeoutMs = options?.timeoutMs ?? defaultTimeoutMs;
    worker.addEventListener("message", this.#handleMessage);
    worker.addEventListener("error", this.#handleFailure);
    worker.addEventListener("messageerror", this.#handleFailure);
  }

  analyze(sourcePath: string, text: string): Promise<SourceAnalysis> {
    if (this.#failure) return Promise.reject(this.#failure);
    const id = this.#nextId++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.#pending.delete(id);
        reject(new Error(`analysis timed out after ${this.#timeoutMs}ms`));
      }, this.#timeoutMs);
      this.#pending.set(id, { resolve, reject, timer });
      this.worker.postMessage({ id, sourcePath, text });
    });
  }

  close() {
    this.worker.removeEventListener("message", this.#handleMessage);
    this.worker.removeEventListener("error", this.#handleFailure);
    this.worker.removeEventListener("messageerror", this.#handleFailure);
    this.worker.terminate();
    this.#rejectAll(new Error("analysis worker closed"));
  }

  #rejectAll(error: Error) {
    for (const pending of this.#pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(error);
    }
    this.#pending.clear();
  }

  readonly #handleFailure = (
    event: MessageEvent<AnalysisWorkerResponse> | ErrorEvent
  ) => {
    const detail = "message" in event && typeof event.message === "string" &&
      event.message !== ""
      ? event.message
      : "the analysis worker failed";
    this.#failure = new Error(detail);
    this.#rejectAll(this.#failure);
  };

  readonly #handleMessage = (
    event: MessageEvent<AnalysisWorkerResponse> | ErrorEvent
  ) => {
    if (!("data" in event)) return;
    const pending = this.#pending.get(event.data.id);
    if (!pending) return;
    this.#pending.delete(event.data.id);
    clearTimeout(pending.timer);
    if ("error" in event.data) {
      pending.reject(new Error(event.data.error));
    } else {
      pending.resolve(event.data.analysis);
    }
  };
}

export function createAnalysisWorkerClient(): AnalysisWorkerClient {
  return new AnalysisWorkerClient(
    new Worker(new URL("./analysis.worker.ts", import.meta.url), {
      type: "module",
      name: "grandleon-source-analysis"
    })
  );
}
