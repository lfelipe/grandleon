#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys


REPO = Path(__file__).resolve().parents[4]
MODULE = REPO / "platform/playstation/mipsync/tools/plan_project.py"
SPEC = importlib.util.spec_from_file_location("grandleon_mipsync_plan", MODULE)
assert SPEC and SPEC.loader
plan_module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(plan_module)


def load(relative: str) -> dict[str, object]:
    value = json.loads((REPO / relative).read_text(encoding="utf-8"))
    assert isinstance(value, dict)
    return value


demo = load("games/demo/source/project.json")
campaign, mode, stages = plan_module.plan(demo)
assert campaign == "demo_campaign"
assert mode == "one"
assert stages == ["demo_campaign/bridge_encounter"]

steel = load("games/steel_and_cedar/source/project.json")
campaign, mode, stages = plan_module.plan(steel)
assert campaign == "steel_and_cedar"
assert mode == "choice"
assert stages == [
    "steel_and_cedar/bell_orchard",
    "steel_and_cedar/cedar_deepwood",
    "steel_and_cedar/cedar_watch",
    "steel_and_cedar/gate_village",
    "steel_and_cedar/twin_gate",
]

alternate = load("games/demo/source/project.json")
campaigns = alternate["campaigns"]
assert isinstance(campaigns, list)
campaigns.insert(0, campaigns.pop(2))
campaign, mode, stages = plan_module.plan(alternate)
assert campaign == "last_signal"
assert mode == "choice"
assert stages == ["last_signal/halcyon_gate"]

invalid = load("games/demo/source/project.json")
invalid_campaigns = invalid["campaigns"]
assert isinstance(invalid_campaigns, list) and isinstance(invalid_campaigns[0], dict)
invalid_campaigns[0].pop("playerMode")
try:
    plan_module.plan(invalid)
except ValueError as error:
    assert "player mode" in str(error)
else:
    raise AssertionError("project without a player mode was accepted")

print("Mipsync project plan passed for one-player, choice, and selected campaigns.")
