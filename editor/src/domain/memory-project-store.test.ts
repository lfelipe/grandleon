// SPDX-License-Identifier: MIT
import { describe, it } from "vitest";
import { MemoryProjectStore } from "./memory-project-store";
import { exerciseProjectStoreContract } from "./project-store.contract";

describe("MemoryProjectStore", () => {
  it("satisfies the project-store contract", async () => {
    await exerciseProjectStoreContract(
      () => new MemoryProjectStore("contract-fixture")
    );
  });
});
