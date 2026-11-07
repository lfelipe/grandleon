# SPDX-License-Identifier: MIT
class_name CampaignReadiness
extends RefCounted


static func playable_export(project: Dictionary, campaign_id := "") -> Dictionary:
	var campaigns_value: Variant = project.get("campaigns", [])
	if not campaigns_value is Array or campaigns_value.is_empty():
		return {"ok": true}
	var campaign_value: Variant = null
	if not campaign_id.is_empty():
		for value: Variant in campaigns_value:
			if value is Dictionary and str(value.get("id", "")) == campaign_id:
				campaign_value = value
				break
	elif campaigns_value.size() == 1:
		campaign_value = campaigns_value[0]
	else:
		return {
			"ok": false,
			"code": "campaign_selection_required",
			"message": "Choose which campaign this player build should start.",
		}
	if campaign_value == null:
		return {
			"ok": false,
			"code": "campaign_not_found",
			"message": "The selected campaign is no longer available.",
		}
	if not campaign_value is Dictionary:
		return {"ok": true}
	var campaign: Dictionary = campaign_value
	var mode := str(campaign.get("playerMode", "one"))
	if mode not in ["two", "choice"]:
		return {"ok": true, "mode": mode, "gaps": []}
	var gaps := two_player_ownership_gaps(campaign)
	if gaps.is_empty():
		return {"ok": true, "mode": mode, "gaps": gaps}
	return {
		"ok": false,
		"code": "player_two_ownership_missing",
		"mode": mode,
		"gaps": gaps,
		"message": (
			"Assign Player 2 an allied character in every battle before exporting. "
			+ "Missing: %s." % ", ".join(gaps)
		),
	}


static func two_player_ownership_gaps(campaign: Dictionary) -> Array[String]:
	var gaps: Array[String] = []
	var flow_value: Variant = campaign.get("flow", {})
	if not flow_value is Dictionary:
		return gaps
	for node_value: Variant in flow_value.get("nodes", []):
		if not node_value is Dictionary:
			continue
		var node: Dictionary = node_value
		if str(node.get("kind", "")) != "encounter":
			continue
		var has_second_player := false
		for placement_value: Variant in node.get("placements", []):
			if not placement_value is Dictionary:
				continue
			var placement: Dictionary = placement_value
			if (
				str(placement.get("side", "first")) == "first"
				and int(placement.get("player", 1)) == 2
				and int(placement.get("minimumPlayers", 1)) <= 2
			):
				has_second_player = true
				break
		if not has_second_player:
			gaps.append(str(node.get("name", node.get("id", "unnamed battle"))))
	return gaps
