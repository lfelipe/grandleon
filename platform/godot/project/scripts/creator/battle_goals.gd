# SPDX-License-Identifier: MIT
extends RefCounted

const CampaignRoster = preload("res://scripts/creator/campaign_roster.gd")

static func describe(objective: Dictionary, project: Dictionary, campaign: Dictionary, encounter: Dictionary, translate: Callable) -> String:
	var kind := str(objective.get("kind", "defeatAllOpponents"))
	var ours := str(objective.get("side", "first")) == "first"
	if kind == "defeatAllOpponents":
		return translate.call("goals.win_defeat_all" if ours else "goals.lose_defeat_all")
	if kind == "surviveRounds":
		return translate.call("goals.win_survive" if ours else "goals.lose_survive", {"rounds": int(objective.get("rounds", 1))})
	if kind in ["defeatTarget", "protectTarget"]:
		var target := target_name(str(objective.get("targetPlacementId", "")), project, campaign, encounter, str(translate.call("map.fallback.character")))
		var wins := ours if kind == "defeatTarget" else not ours
		return translate.call("goals.win_target" if wins else "goals.lose_target", {"target": target})
	return str(objective.get("name", ""))

static func target_name(identity: String, project: Dictionary, campaign: Dictionary, encounter: Dictionary, fallback: String) -> String:
	for placement: Dictionary in encounter.get("placements", []):
		if str(placement.get("memberId", placement.get("id", ""))) != identity: continue
		var member_id := str(placement.get("memberId", ""))
		if not member_id.is_empty():
			for member: Dictionary in CampaignRoster.members(campaign):
				if str(member.get("id", "")) == member_id:
					return str(member.get("name", fallback))
		for role: Dictionary in project.get("unitTypes", []):
			if str(role.get("id", "")) == str(placement.get("unitTypeId", "")):
				return str(role.get("name", fallback))
	return fallback
