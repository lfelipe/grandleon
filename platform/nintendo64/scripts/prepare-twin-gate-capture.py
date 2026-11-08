#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Write a direct-entry copy of Steel and Cedar for the renderer capture.

Only the campaign entry node changes. The map, encounter, placements, rules,
objectives, art joins and all source identities remain the authored project.
The derived file lives in the build directory and is never a second campaign
source.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()

    project = json.loads(arguments.source.read_text(encoding="utf-8"))
    campaigns = project.get("campaigns", [])
    if len(campaigns) != 1:
        raise ValueError("Steel and Cedar must keep one campaign")
    flow = campaigns[0].get("flow", {})
    nodes = flow.get("nodes", [])
    target = next(
        (
            node for node in nodes
            if node.get("id") == "twin_gate" and node.get("kind") == "encounter"
        ),
        None,
    )
    if target is None or target.get("mapId") != "twin_gate":
        raise ValueError("the authored Twin Gate encounter is missing")
    flow["entryNodeId"] = "twin_gate"
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        json.dumps(project, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(f"Twin Gate capture project: {arguments.output}")


if __name__ == "__main__":
    main()
