// SPDX-License-Identifier: MIT
import { createApp, nextTick } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { CampaignRosterMember } from "../generated/source-v1";
import RosterMemberEditor from "./RosterMemberEditor.vue";

afterEach(() => document.body.replaceChildren());

const unitTypes = [
  { id: "guardian", name: "Guardian" },
  { id: "archer", name: "Archer" }
];

function mount(
  members: readonly CampaignRosterMember[],
  overrides: {
    idPrefix?: string;
    memberWord?: string;
    otherIds?: readonly string[];
  } = {}
) {
  const host = document.createElement("div");
  document.body.append(host);
  const onUpdate = vi.fn();
  const onCreateUnitType = vi.fn();
  const app = createApp(RosterMemberEditor, {
    members,
    unitTypes,
    idPrefix: overrides.idPrefix ?? "company",
    heading: "The company this campaign starts with",
    help: "Who marches out on the first battle.",
    memberWord: overrides.memberWord ?? "member",
    otherIds: overrides.otherIds,
    onUpdate,
    onCreateUnitType
  });
  app.mount(host);
  return { app, host, onUpdate, onCreateUnitType };
}

function field(host: HTMLElement, id: string): HTMLInputElement {
  const found = host.querySelector<HTMLInputElement>(`#${id}`);
  if (!found) throw new Error(`field '${id}' not found`);
  return found;
}

function type(host: HTMLElement, id: string, value: string) {
  const control = field(host, id);
  control.value = value;
  control.dispatchEvent(new Event("change", { bubbles: true }));
}

function lastMembers(
  onUpdate: ReturnType<typeof vi.fn>
): CampaignRosterMember[] {
  return onUpdate.mock.lastCall?.[0] as CampaignRosterMember[];
}

const wren: CampaignRosterMember = {
  id: "wren",
  name: "Wren",
  unitTypeId: "archer"
};

describe("RosterMemberEditor specificity", () => {
  it("gives every difference a label a keyboard can reach it by", () => {
    const { app, host } = mount([wren]);
    for (const id of [
      "company-0-stat-health",
      "company-0-stat-movement",
      "company-0-stat-strength",
      "company-0-stat-defense",
      "company-0-stat-resistance",
      "company-0-stat-skill",
      "company-0-stat-luck",
      "company-0-stat-evasion",
      "company-0-stat-magic",
      "company-0-stat-actionPoints",
      "company-0-stat-speed",
      "company-0-range-bonus"
    ]) {
      const control = host.querySelector(`#${id}`);
      expect(control, id).not.toBeNull();
      expect(host.querySelector(`label[for="${id}"]`), id).not.toBeNull();
    }
    app.unmount();
  });

  it("writes one signed difference and nothing else", async () => {
    const { app, host, onUpdate } = mount([wren]);
    type(host, "company-0-stat-strength", "3");
    await nextTick();
    expect(lastMembers(onUpdate)).toEqual([{
      id: "wren",
      name: "Wren",
      unitTypeId: "archer",
      specificity: { stats: { strength: 3 } }
    }]);

    type(host, "company-0-stat-defense", "-2");
    await nextTick();
    expect(lastMembers(onUpdate)[0]?.specificity)
      .toEqual({ stats: { defense: -2 } });
    app.unmount();
  });

  it("removes a cleared difference rather than writing 0 in its place", async () => {
    const { app, host, onUpdate } = mount([{
      ...wren,
      specificity: { stats: { health: 2, speed: -1 } }
    }]);
    type(host, "company-0-stat-health", "");
    await nextTick();
    // An absent stat says something different from a stat holding nothing, and
    // 0 is not a difference: the key goes rather than the value changing.
    const stats = lastMembers(onUpdate)[0]?.specificity?.stats;
    expect(stats).toEqual({ speed: -1 });
    expect(stats).not.toHaveProperty("health");
    app.unmount();
  });

  it("takes the stat block away with the last difference in it", async () => {
    const { app, host, onUpdate } = mount([{
      ...wren,
      specificity: { stats: { health: 2 }, rangeBonus: 1 }
    }]);
    type(host, "company-0-stat-health", "");
    await nextTick();
    const specificity = lastMembers(onUpdate)[0]?.specificity;
    expect(specificity).toEqual({ rangeBonus: 1 });
    expect(specificity).not.toHaveProperty("stats");
    app.unmount();
  });

  it("takes the whole specificity away when the last thing in it is cleared", async () => {
    const { app, host, onUpdate } = mount([{
      ...wren,
      specificity: { stats: { health: 2 } }
    }]);
    type(host, "company-0-stat-health", "");
    await nextTick();
    // Somebody with no differences left is exactly their character, and an
    // absent specificity is how that is said.
    expect(lastMembers(onUpdate)).toEqual([wren]);
    expect(lastMembers(onUpdate)[0]).not.toHaveProperty("specificity");
    app.unmount();
  });

  it("keeps the stats when the reach is cleared, and clears down to nothing", async () => {
    const { app, host, onUpdate } = mount([{
      ...wren,
      specificity: { stats: { magic: 4 }, rangeBonus: 2 }
    }, {
      id: "kesh",
      name: "Kesh",
      unitTypeId: "guardian",
      specificity: { rangeBonus: 2 }
    }]);
    type(host, "company-0-range-bonus", "");
    await nextTick();
    expect(lastMembers(onUpdate)[0]?.specificity)
      .toEqual({ stats: { magic: 4 } });

    type(host, "company-1-range-bonus", "");
    await nextTick();
    expect(lastMembers(onUpdate)[1]).not.toHaveProperty("specificity");
    // Editing one member leaves the other exactly as it was.
    expect(lastMembers(onUpdate)[0]?.specificity)
      .toEqual({ stats: { magic: 4 }, rangeBonus: 2 });
    app.unmount();
  });

  it("round-trips a reach bonus through the control it is shown in", async () => {
    const { app, host, onUpdate } = mount([{ ...wren, specificity: { rangeBonus: 3 } }]);
    expect(field(host, "company-0-range-bonus").value).toBe("3");
    type(host, "company-0-range-bonus", "7");
    await nextTick();
    expect(lastMembers(onUpdate)).toEqual([{
      id: "wren",
      name: "Wren",
      unitTypeId: "archer",
      specificity: { rangeBonus: 7 }
    }]);
    app.unmount();
  });

  it("shows the differences a member already carries, and counts them", () => {
    const { app, host } = mount([{
      ...wren,
      specificity: { stats: { health: 2, luck: -3 }, rangeBonus: 1 }
    }]);
    expect(field(host, "company-0-stat-health").value).toBe("2");
    expect(field(host, "company-0-stat-luck").value).toBe("-3");
    expect(field(host, "company-0-stat-speed").value).toBe("");
    expect(host.textContent).toContain("3 written");
    app.unmount();
  });

  it("says why a difference of 0, and a reach of 0 or out of range, cannot stay", () => {
    const { app, host } = mount([
      { ...wren, specificity: { stats: { luck: 0 } } },
      { id: "kesh", name: "Kesh", unitTypeId: "guardian", specificity: { rangeBonus: 0 } },
      { id: "orin", name: "Orin", unitTypeId: "guardian", specificity: { rangeBonus: 40 } },
      { id: "sable", name: "Sable", unitTypeId: "guardian", specificity: {} }
    ]);
    const text = host.textContent!;
    expect(text).toContain("Luck is 0, which changes nothing.");
    expect(text).toContain("A range bonus of 0 adds nothing.");
    expect(text).toContain("A range bonus is a whole number from 1 to 32.");
    // Marked as particular without saying how is a claim with nothing behind it.
    expect(text).toContain(
      "This member is written as more than their character without saying how."
    );
    expect(host.querySelectorAll("[role=\"alert\"]").length).toBe(4);
    app.unmount();
  });

  it("says nothing about a member who is exactly their character", () => {
    const { app, host } = mount([wren]);
    expect(host.querySelector("[role=\"alert\"]")).toBeNull();
    // Nothing is written, so the folded summary counts nothing.
    expect(host.textContent).not.toContain("1 written");
    expect(field(host, "company-0-range-bonus").value).toBe("");
    app.unmount();
  });

  it("gives a recruit the same two knobs under their own control names", async () => {
    const { app, host, onUpdate } = mount([{
      id: "mirea",
      name: "Mirea",
      unitTypeId: "archer",
      specificity: { stats: { skill: 5 } }
    }], { idPrefix: "recruit", memberWord: "recruit" });
    // A recruit is a member of the company from the moment they join, so the
    // same list edits them; only the control names differ, which is what keeps
    // two mounted lists on one page apart.
    expect(host.querySelector("#company-0-stat-skill")).toBeNull();
    expect(field(host, "recruit-0-stat-skill").value).toBe("5");

    type(host, "recruit-0-range-bonus", "2");
    await nextTick();
    expect(lastMembers(onUpdate)).toEqual([{
      id: "mirea",
      name: "Mirea",
      unitTypeId: "archer",
      specificity: { stats: { skill: 5 }, rangeBonus: 2 }
    }]);

    type(host, "recruit-0-stat-skill", "0");
    await nextTick();
    expect(lastMembers(onUpdate)[0]?.specificity?.stats).toEqual({ skill: 0 });
    app.unmount();
  });

  it("keeps focus while a difference is typed digit by digit", async () => {
    const { app, host } = mount([wren]);
    const health = field(host, "company-0-stat-health");
    health.focus();
    for (const partial of ["-", "-1", "-12"]) {
      health.value = partial;
      health.dispatchEvent(new Event("input", { bubbles: true }));
      await nextTick();
    }
    expect(host.querySelector("#company-0-stat-health")).toBe(health);
    expect(document.activeElement).toBe(health);
    app.unmount();
  });
});
