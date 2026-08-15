// SPDX-License-Identifier: MIT
// Instantiates the WebAssembly simulation once for the whole test run.
//
// The engine is asynchronous to load but synchronous to use, so loading it here
// keeps every test that plays an encounter free of setup ceremony.

import { initEncounterEngine } from "./domain/encounter-simulation";

await initEncounterEngine();
