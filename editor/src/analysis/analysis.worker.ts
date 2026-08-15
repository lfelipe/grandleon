// SPDX-License-Identifier: MIT
import { analyzeSourceProject } from "./source-analysis";
import type {
  AnalysisWorkerRequest,
  AnalysisWorkerResponse
} from "./analysis-worker-protocol";

interface WorkerScope {
  addEventListener(
    type: "message",
    listener: (event: MessageEvent<AnalysisWorkerRequest>) => void
  ): void;
  postMessage(message: AnalysisWorkerResponse): void;
}

const scope = self as unknown as WorkerScope;

scope.addEventListener("message", (event) => {
  const request = event.data;
  let response: AnalysisWorkerResponse;
  try {
    response = {
      id: request.id,
      analysis: analyzeSourceProject(request.sourcePath, request.text)
    };
  } catch (error) {
    response = {
      id: request.id,
      error: error instanceof Error ? error.message : String(error)
    };
  }
  scope.postMessage(response);
});
