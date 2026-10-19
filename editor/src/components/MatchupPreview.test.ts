// SPDX-License-Identifier: MIT
import { createApp, nextTick } from "vue";
import { afterEach, describe, expect, it } from "vitest";
import type { SourceProject } from "../generated/source-v1";
import MatchupPreview from "./MatchupPreview.vue";

afterEach(() => document.body.replaceChildren());

const project: SourceProject = {
  schemaVersion: "1.2.0",
  packageId: "11111111-1111-4111-8111-111111111111",
  gameId: "test_game",
  title: "Test Game",
  contentRevision: "0.1.0",
  weaponAdvantage: { damage: 2, accuracy: 15 },
  weaponTypes: [
    { id: "blade", name: "Blade", strongAgainst: ["axe"] },
    { id: "axe", name: "Axe" }
  ],
  weapons: [
    {
      id: "iron_blade",
      name: "Iron Blade",
      weaponTypeId: "blade",
      power: 5,
      accuracy: 90,
      minimumRange: 1,
      maximumRange: 1
    },
    {
      id: "raider_axe",
      name: "Raider Axe",
      weaponTypeId: "axe",
      power: 7,
      accuracy: 75,
      minimumRange: 1,
      maximumRange: 1
    }
  ],
  abilities: [
    {
      id: "ember_bolt",
      name: "Ember Bolt",
      kind: "damage",
      damageType: "magical",
      power: 6,
      accuracy: 95,
      minimumRange: 1,
      maximumRange: 2
    }
  ],
  classes: [
    {
      id: "guard",
      name: "Guard",
      baseStats: {
        health: 20,
        strength: 6,
        defense: 4,
        resistance: 3,
        movement: 4,
        actionPoints: 2,
        skill: 5,
        luck: 2,
        evasion: 3,
        magic: 4,
        speed: 5
      }
    },
    {
      id: "raider",
      name: "Raider",
      baseStats: {
        health: 24,
        strength: 8,
        defense: 3,
        resistance: 1,
        movement: 4,
        actionPoints: 2,
        skill: 2,
        luck: 1,
        evasion: 2,
        magic: 0,
        speed: 4
      }
    }
  ],
  unitTypes: [
    {
      id: "dawn_guard",
      name: "Dawn Guard",
      classId: "guard",
      startingWeaponIds: ["iron_blade"],
      abilityIds: ["ember_bolt"]
    },
    {
      id: "iron_raider",
      name: "Iron Raider",
      classId: "raider",
      startingWeaponIds: ["raider_axe"]
    }
  ],
  itemTypes: [],
  items: [],
  maps: []
};

describe("MatchupPreview", () => {
  it("shows exact weapon, triangle, and ability exchange numbers between two characters", async () => {
    const host = document.createElement("div");
    document.body.append(host);
    const app = createApp(MatchupPreview, { project });
    app.mount(host);
    await nextTick();

    // Dawn Guard -> Iron Raider:
    // strength 6 + power 5 + advantage 2 - defense 3 = 10 damage (3 hits to defeat 24 health)
    // hit: 90 + skill 5 + luck 2 + advantage 15 - evasion 2 - luck 1 = 109 -> 100%
    expect(host.textContent).toContain("Dawn Guard → Iron Raider");
    expect(host.textContent).toContain("10 damage per hit");
    expect(host.textContent).toContain("100% hit, 3 hits to defeat 24 health");
    expect(host.textContent).toContain("Weapon triangle: advantage");

    // Ember Bolt: magic 4 + power 6 - resistance 1 = 9 damage
    expect(host.textContent).toContain("Ember Bolt");
    expect(host.textContent).toContain("9 damage");

    // Iron Raider -> Dawn Guard:
    // strength 8 + power 7 - advantage 2 - defense 4 = 9 damage (3 hits to defeat 20 health)
    // hit: 75 + skill 2 + luck 1 - advantage 15 - evasion 3 - luck 2 = 58%
    expect(host.textContent).toContain("Iron Raider → Dawn Guard");
    expect(host.textContent).toContain("9 damage per hit");
    expect(host.textContent).toContain("58% hit, 3 hits to defeat 20 health");
    expect(host.textContent).toContain("Weapon triangle: disadvantage");

    app.unmount();
  });
});
