// SPDX-License-Identifier: MIT
import type { SourceAnalysis } from "./source-analysis";

export interface AnalysisWorkerRequest {
  readonly id: number;
  readonly sourcePath: string;
  readonly text: string;
}

export type AnalysisWorkerResponse =
  | { readonly id: number; readonly analysis: SourceAnalysis }
  | { readonly id: number; readonly error: string };
