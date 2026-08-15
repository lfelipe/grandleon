// SPDX-License-Identifier: MIT
import maintainedDemoSource from "../../games/demo/source/project.json";
import tarnholtSource from "../../games/tarnholt/source/project.json";
import type { SourceProject } from "./generated/source-v1";
import { decodeSourceProject } from "./domain/source-project-document";

export interface SampleProject {
  id: string;
  title: string;
  summary: string;
  create: () => SourceProject;
}

/**
 * Returns a fresh, schema-validated copy of a checked-in sample project.
 *
 * Importing the canonical sources directly makes Vite bundle them into the
 * static editor application. There is no second checked-in copy that can drift,
 * and the samples remain available from root, sub-path, and offline
 * deployments.
 */
function decode(source: unknown): SourceProject {
  return decodeSourceProject(new TextEncoder().encode(JSON.stringify(source)));
}

export const sampleProjects: readonly SampleProject[] = [
  {
    id: "tarnholt",
    title: "The Tarnholt Line",
    summary: "Six maps, two sides, and a story in between.",
    create: () => decode(tarnholtSource)
  },
  {
    id: "demo",
    title: "The Bridge at Dawn",
    summary: "One small fight, whole. The shortest game there is.",
    create: () => decode(maintainedDemoSource)
  }
];

export function createSampleProject(id: string): SourceProject {
  const sample = sampleProjects.find((candidate) => candidate.id === id);
  if (!sample) throw new Error(`Unknown sample project '${id}'.`);
  return sample.create();
}

/** The maintained demo, kept as its own entry point for conformance tests. */
export function createDemoProject(): SourceProject {
  return createSampleProject("demo");
}
