// SPDX-License-Identifier: MIT
import { describe, expect, it } from "vitest";
import {
  RomService,
  RomServiceError,
  type RomBuildStatus
} from "./rom-service";

// A fetch that answers a scripted sequence, so every state a build can be in
// is reachable without a server and without a container.
function scriptedFetch(
  routes: Record<string, Array<{ status: number; body: unknown; blob?: Uint8Array }>>
) {
  const seen: string[] = [];
  const fetchMock = (async (input: RequestInfo | URL) => {
    const url = String(input);
    seen.push(url);
    const key = Object.keys(routes).find((route) => url.startsWith(route));
    if (key === undefined) throw new TypeError("Failed to fetch");
    const queued = routes[key]!;
    const answer = queued.length > 1 ? queued.shift()! : queued[0]!;
    if (answer.blob !== undefined) {
      return new Response(answer.blob as BlobPart, { status: answer.status });
    }
    return new Response(JSON.stringify(answer.body), {
      status: answer.status,
      headers: { "content-type": "application/json" }
    });
  }) as unknown as typeof globalThis.fetch;
  return { fetchMock, seen };
}

const building = (over: Partial<RomBuildStatus> = {}): RomBuildStatus => ({
  id: "job-1",
  state: "building",
  console: "nintendo64",
  title: "A Game",
  campaign: "a_campaign",
  position: 0,
  waiting: 1,
  bytes: 0,
  md5: "",
  log: "",
  error: null,
  ...over
});

const noSleep = async () => {};

describe("the editor's side of the ROM build service", () => {
  it("asks a relative path, so the page's connect-src stays 'self'", async () => {
    const { fetchMock, seen } = scriptedFetch({
      "/api/n64/health": [{ status: 200, body: { ready: true } }]
    });
    await new RomService({ fetch: fetchMock }).health();
    // The assertion that matters is not that the call happened but that it
    // named no origin: a URL with a host in it would need `connect-src`
    // widened, and widening it is exactly what this design avoids.
    expect(seen).toEqual(["/api/n64/health"]);
    for (const url of seen) expect(url).not.toMatch(/^https?:/);
  });

  it("reports the service being absent rather than a network error", async () => {
    const fetchMock = (async () => {
      throw new TypeError("Failed to fetch");
    }) as unknown as typeof globalThis.fetch;
    const health = await new RomService({ fetch: fetchMock }).health();
    expect(health.ready).toBe(false);
    expect(health.code).toBe("rom_service_unreachable");
    // And it says what to do about it, which a network stack's message cannot.
    expect(health.message).toMatch(/tools\/rom_service\/serve\.mjs/);
  });

  it("treats a proxy answering HTML as the service not being there", async () => {
    // This is what `vite preview` does when nothing is behind the proxy, and
    // it is the state the browser gate runs in.
    const fetchMock = (async () =>
      new Response("<!doctype html><title>404</title>", {
        status: 404,
        headers: { "content-type": "text/html" }
      })) as unknown as typeof globalThis.fetch;
    const health = await new RomService({ fetch: fetchMock }).health();
    expect(health.ready).toBe(false);
    expect(health.code).toBe("rom_service_unreachable");
  });

  it("follows a build to its end and reports every state on the way", async () => {
    const rom = new Uint8Array([0x80, 0x37, 0x12, 0x40]);
    const { fetchMock } = scriptedFetch({
      "/api/n64/build/job-1/rom": [{ status: 200, body: null, blob: rom }],
      "/api/n64/build/job-1": [
        { status: 200, body: building({ state: "building" }) },
        { status: 200, body: building({ state: "done", bytes: 4, md5: "ab" }) }
      ],
      "/api/n64/build": [{ status: 202, body: building({ state: "queued" }) }]
    });
    const progress: string[] = [];
    const service = new RomService({ fetch: fetchMock, pollMilliseconds: 0 });
    const result = await service.build(
      "{}", (status) => progress.push(status.state), noSleep
    );
    expect(progress).toEqual(["queued", "building", "done"]);
    expect(result.rom).toEqual(rom);
    expect(result.status.md5).toBe("ab");
  });

  it("raises a refusal with the service's own code and words", async () => {
    const { fetchMock } = scriptedFetch({
      "/api/n64/build": [{
        status: 400,
        body: {
          code: "project_without_campaign",
          message: "This project has no campaign.",
          detail: "the project carries no campaigns"
        }
      }]
    });
    const service = new RomService({ fetch: fetchMock, pollMilliseconds: 0 });
    await expect(service.build("{}", () => {}, noSleep)).rejects.toSatisfy(
      (error: unknown) => {
        expect(error).toBeInstanceOf(RomServiceError);
        const refusal = (error as RomServiceError).refusal;
        expect(refusal.code).toBe("project_without_campaign");
        expect(refusal.detail).toBe("the project carries no campaigns");
        return true;
      }
    );
  });

  it("carries the toolchain's own output when a build fails", async () => {
    const failed = building({
      state: "failed",
      error: {
        code: "rom_build_failed",
        message: "The Nintendo 64 build failed.",
        detail: "mips64-elf-gcc: internal compiler error"
      }
    });
    const { fetchMock } = scriptedFetch({
      "/api/n64/build/job-1": [{ status: 200, body: failed }],
      "/api/n64/build": [{ status: 202, body: building() }]
    });
    const service = new RomService({ fetch: fetchMock, pollMilliseconds: 0 });
    await expect(service.build("{}", () => {}, noSleep)).rejects.toSatisfy(
      (error: unknown) => {
        const refusal = (error as RomServiceError).refusal;
        expect(refusal.code).toBe("rom_build_failed");
        // The compiler's words reach the author unedited. A summary here
        // would throw away the only part they can act on.
        expect(refusal.detail).toContain("mips64-elf-gcc");
        return true;
      }
    );
  });

  it("reports a place in the queue rather than an unknowable time", async () => {
    const { fetchMock } = scriptedFetch({
      "/api/n64/build/job-1/rom": [
        { status: 200, body: null, blob: new Uint8Array([1]) }
      ],
      "/api/n64/build/job-1": [{ status: 200, body: building({ state: "done" }) }],
      "/api/n64/build": [{
        status: 202,
        body: building({ state: "queued", position: 2, waiting: 3 })
      }]
    });
    const seen: RomBuildStatus[] = [];
    const service = new RomService({ fetch: fetchMock, pollMilliseconds: 0 });
    await service.build("{}", (status) => seen.push(status), noSleep);
    expect(seen[0]!.position).toBe(2);
  });
});
