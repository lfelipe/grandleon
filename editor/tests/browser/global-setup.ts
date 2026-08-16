// SPDX-License-Identifier: MIT
import net from "node:net";
import process from "node:process";

// Proves the machine is not answering where this suite needs nothing to answer,
// before a browser starts.
//
// One test — "offers the Nintendo 64 ROM, and says why it cannot build one
// here" — asserts that the editor reports the ROM build service absent. It can
// only assert that while the service really is absent, and the port
// `vite.config.ts` defaults to, 4699, is where a person's service lives. It is
// often up, because running it is how the editor is used. `playwright.config.ts`
// therefore points the proxy at a port of this suite's own; this is where that
// choice stops being a hope.
//
// It refuses rather than working around it. Picking another port on the spot
// would make the run depend on which port happened to be free, and this suite's
// whole complaint about 4699 is that it depended on what the machine happened to
// be running.
export default async function globalSetup(): Promise<void> {
  const port = Number(process.env.GRANDLEON_ROM_SERVICE_PORT);
  if (!Number.isInteger(port) || port <= 0 || port > 65535) {
    throw new Error(
      `GRANDLEON_ROM_SERVICE_PORT is '${process.env.GRANDLEON_ROM_SERVICE_PORT}'`
        + ", which is not a port. playwright.config.ts sets it."
    );
  }

  const answered = await new Promise<boolean>((resolve) => {
    const socket = net.connect({ host: "127.0.0.1", port });
    const settle = (result: boolean) => {
      socket.destroy();
      resolve(result);
    };
    socket.setTimeout(2_000);
    socket.on("connect", () => settle(true));
    // Refused, unreachable, or slow enough that no ROM service is behind it.
    socket.on("error", () => settle(false));
    socket.on("timeout", () => settle(false));
  });

  if (answered) {
    throw new Error(
      `Something is listening on 127.0.0.1:${port}, which this suite needs `
        + "free: it checks that the editor says a ROM cannot be built when no "
        + "build service is running, and a service there would make that test "
        + "fail for a reason that is not the editor's.\n"
        + "Stop it, or set GRANDLEON_ROM_SERVICE_PORT to a port nothing is on."
    );
  }
}
