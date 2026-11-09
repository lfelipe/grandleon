#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Describe the selected project's first campaign for the PS1 disc builder."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re


IDENTIFIER = re.compile(r"^[a-z][a-z0-9_]*$")
PLAYER_MODES = {"one", "two", "choice"}


def require_identifier(value: object, label: str) -> str:
    text = str(value or "")
    if not IDENTIFIER.fullmatch(text):
        raise ValueError(f"{label} is not a portable identifier: {text!r}")
    return text


def plan(project: dict[str, object]) -> tuple[str, str, list[str]]:
    campaigns = project.get("campaigns")
    if not isinstance(campaigns, list) or not campaigns:
        raise ValueError("the selected project has no campaign")
    campaign = campaigns[0]
    if not isinstance(campaign, dict):
        raise ValueError("the selected campaign is not an object")
    campaign_id = require_identifier(campaign.get("id"), "campaign ID")
    player_mode = str(campaign.get("playerMode") or "")
    if player_mode not in PLAYER_MODES:
        raise ValueError(
            "the selected campaign needs a one, two, or choice player mode"
        )
    flow = campaign.get("flow")
    if not isinstance(flow, dict):
        raise ValueError("the selected campaign has no flow")
    nodes = flow.get("nodes")
    if not isinstance(nodes, list):
        raise ValueError("the selected campaign flow has no nodes")
    encounters: list[str] = []
    seen: set[str] = set()
    for value in nodes:
        if not isinstance(value, dict) or str(value.get("kind")) != "encounter":
            continue
        node_id = require_identifier(value.get("id"), "encounter node ID")
        if node_id in seen:
            raise ValueError(f"encounter node ID is repeated: {node_id}")
        seen.add(node_id)
        encounters.append(f"{campaign_id}/{node_id}")
    if not encounters:
        raise ValueError("the selected campaign has no playable battle")
    if len(encounters) > 99:
        raise ValueError("PlayStation campaigns support at most 99 battles")
    return campaign_id, player_mode, encounters


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("project", type=Path)
    args = parser.parse_args()
    document = json.loads(args.project.read_text(encoding="utf-8"))
    if not isinstance(document, dict):
        raise ValueError("project source is not an object")
    campaign_id, player_mode, encounters = plan(document)
    print(f"campaign\t{campaign_id}\t{player_mode}")
    for index, encounter in enumerate(encounters, start=1):
        print(f"stage\t{index:02d}\t{encounter}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
