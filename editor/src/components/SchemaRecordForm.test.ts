// SPDX-License-Identifier: MIT
import { createApp, h, nextTick, ref } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import { sourceRecordFields } from "../domain/source-form-model";
import SchemaRecordForm from "./SchemaRecordForm.vue";

afterEach(() => document.body.replaceChildren());

interface FormHandle {
  flush(): boolean;
}

function mount(props: Record<string, unknown>) {
  const host = document.createElement("div");
  document.body.append(host);
  const onSubmit = vi.fn();
  const app = createApp(SchemaRecordForm, {
    ...props,
    onSubmit
  });
  const form = app.mount(host) as unknown as FormHandle;
  return { app, host, onSubmit, form };
}

describe("SchemaRecordForm", () => {
  it("renders schema limits and submits grouped nested edits", async () => {
    const { app, host, onSubmit } = mount({
      heading: "Vanguard",
      fields: sourceRecordFields("classes"),
      modelValue: {
        id: "vanguard",
        name: "Vanguard",
        baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
      },
      referenceChoices: { weapon_type: [] }
    });
    const health = host.querySelector<HTMLInputElement>("#field-baseStats-health")!;
    expect(health.min).toBe("1");
    expect(health.max).toBe("32767");
    health.value = "20";
    health.dispatchEvent(new Event("input", { bubbles: true }));
    host.querySelector("form")!.dispatchEvent(
      new Event("submit", { bubbles: true, cancelable: true })
    );
    await nextTick();
    expect(onSubmit.mock.calls[0]?.[0]).toEqual(expect.objectContaining({
      baseStats: expect.objectContaining({ health: 20 })
    }));
    app.unmount();
  });

  it("uses typed choices and preserves an omitted legacy optional list", async () => {
    const { app, host, onSubmit } = mount({
      heading: "Vanguard",
      fields: sourceRecordFields("classes"),
      modelValue: {
        id: "vanguard",
        name: "Vanguard",
        baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
      },
      referenceChoices: {
        weapon_type: [{ id: "blade", name: "Blade" }]
      }
    });
    expect(host.textContent).toContain("Configure this field");
    expect(host.textContent).not.toContain("Blade (blade)");
    host.querySelector("form")!.dispatchEvent(
      new Event("submit", { bubbles: true, cancelable: true })
    );
    await nextTick();
    expect(onSubmit.mock.calls[0]?.[0].allowedWeaponTypeIds).toBeUndefined();
    app.unmount();
  });

  it("explains and disables incompatible reference choices", () => {
    const { app, host } = mount({
      heading: "Weapon",
      fields: sourceRecordFields("weapons"),
      modelValue: { id: "bow", name: "Bow", power: 2, range: 3 },
      referenceChoices: {
        weapon_type: [{
          id: "bow",
          name: "Bow",
          compatibility: "incompatible",
          explanation: "The selected class permits blades only."
        }]
      }
    });
    expect(host.querySelector('option[value="bow"]')?.hasAttribute("disabled"))
      .toBe(true);
    expect(host.textContent).toContain("permits blades only");
    app.unmount();
  });

  it("searches single references, explains missing values, and requests related creation", async () => {
    const onCreateRelated = vi.fn();
    const host = document.createElement("div");
    document.body.append(host);
    const app = createApp(SchemaRecordForm, {
      heading: "Unit",
      fields: sourceRecordFields("unitTypes"),
      modelValue: { id: "soldier", name: "Soldier", classId: "missing" },
      referenceChoices: {
        class: [
          { id: "vanguard", name: "Vanguard" },
          { id: "ranger", name: "Ranger" }
        ],
        weapon: [],
        item: []
      },
      onCreateRelated
    });
    app.mount(host);
    expect(host.textContent).toContain("does not exist in this project");
    const search = host.querySelector<HTMLInputElement>("#field-classId-search")!;
    search.value = "range";
    search.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    expect(host.querySelector('option[value="ranger"]')).not.toBeNull();
    expect(host.querySelector('option[value="vanguard"]')).toBeNull();
    const create = [...host.querySelectorAll("button")].find(
      (item) => item.textContent?.includes("Create related class")
    )!;
    create.click();
    expect(onCreateRelated).toHaveBeenCalledWith("class");
    app.unmount();
  });

  it("edits booleans as checkboxes and drops an optional one turned off", async () => {
    const { app, host, onSubmit } = mount({
      heading: "Vanguard",
      fields: sourceRecordFields("classes"),
      modelValue: {
        id: "vanguard",
        name: "Vanguard",
        baseStats: { health: 10, movement: 4, strength: 3, defense: 2 },
        actsAfterAttacking: true
      },
      referenceChoices: { weapon_type: [] }
    });
    const submit = () => host.querySelector("form")!.dispatchEvent(
      new Event("submit", { bubbles: true, cancelable: true })
    );
    const toggle = host.querySelector<HTMLInputElement>(
      "#field-actsAfterAttacking"
    )!;
    expect(toggle.type).toBe("checkbox");
    expect(toggle.checked).toBe(true);
    expect(host.textContent).toContain("Keeps acting after attacking");

    toggle.click();
    await nextTick();
    submit();
    await nextTick();
    // Off reads as an absent field, matching a record that never set it.
    expect("actsAfterAttacking" in onSubmit.mock.calls[0]![0]).toBe(false);

    toggle.click();
    await nextTick();
    submit();
    await nextTick();
    expect(onSubmit.mock.calls[1]?.[0].actsAfterAttacking).toBe(true);
    app.unmount();
  });

  it("edits enums as selects with explanatory options", async () => {
    const { app, host, onSubmit } = mount({
      heading: "Fireball",
      fields: sourceRecordFields("abilities"),
      modelValue: {
        id: "fireball",
        name: "Fireball",
        kind: "damage",
        power: 3,
        minimumRange: 1,
        maximumRange: 2
      }
    });
    const kind = host.querySelector<HTMLSelectElement>("#field-kind")!;
    expect(kind.tagName).toBe("SELECT");
    expect(kind.value).toBe("damage");
    const labels = [...kind.options].map((option) => option.textContent?.trim());
    expect(labels.join(" ")).toContain("heals");

    const damageType = host.querySelector<HTMLSelectElement>("#field-damageType")!;
    expect([...damageType.options][0]?.value).toBe("");
    damageType.value = "magical";
    damageType.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    host.querySelector("form")!.dispatchEvent(
      new Event("submit", { bubbles: true, cancelable: true })
    );
    await nextTick();
    expect(onSubmit.mock.calls[0]?.[0].damageType).toBe("magical");
    app.unmount();
  });

  it("says a character follows the game, and clears back to it", async () => {
    // A per-character choice that falls back to a game-wide one is not unset:
    // it follows the game. A control that could not say so would invite an
    // author to pick the value they already have, and picking it writes an
    // override the moment it is touched, which is the defect the game-wide
    // controls were fixed for once already.
    const { app, host, onSubmit } = mount({
      heading: "Risen Soldier",
      fields: sourceRecordFields("unitTypes"),
      modelValue: {
        id: "risen",
        name: "Risen Soldier",
        classId: "vanguard",
        characterStyleId: "undead"
      },
      referenceChoices: { class: [{ value: "vanguard", label: "Vanguard" }] }
    });
    for (const name of ["characterStyleId"]) {
      const select = host.querySelector<HTMLSelectElement>(`#field-${name}`)!;
      expect(select.tagName).toBe("SELECT");
      expect([...select.options][0]?.value).toBe("");
      expect([...select.options][0]?.textContent?.trim())
        .toBe("Follow the game setting");
    }
    const style = host.querySelector<HTMLSelectElement>(
      "#field-characterStyleId"
    )!;
    expect(style.value).toBe("undead");
    // The whole menu is offered, because the game's style is a default and
    // never a gate on what a character may be drawn in.
    expect([...style.options].map((option) => option.value)).toEqual([
      "", "medieval", "scifi", "mythical", "nature", "sengoku", "undead",
      "pirates"
    ]);

    // Naming a figure writes that field and touches nothing else.
    const figure = host.querySelector<HTMLSelectElement>(
      "#field-characterFigureId"
    )!;
    figure.value = "second";
    figure.dispatchEvent(new Event("change", { bubbles: true }));
    // Choosing the empty option deletes the style rather than writing one, so
    // the record goes back to saying nothing at all about it.
    style.value = "";
    style.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    host.querySelector("form")!.dispatchEvent(
      new Event("submit", { bubbles: true, cancelable: true })
    );
    await nextTick();
    const saved = onSubmit.mock.calls[0]?.[0];
    expect(saved.characterFigureId).toBe("second");
    expect("characterStyleId" in saved).toBe(false);
    app.unmount();
  });

  it("keeps unsaved edits when an equivalent record object arrives", async () => {
    const record = () => ({
      id: "vanguard",
      name: "Vanguard",
      baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
    });
    const model = ref<Record<string, unknown>>(record());
    const host = document.createElement("div");
    document.body.append(host);
    const onSubmit = vi.fn();
    const app = createApp({
      setup: () => () => h(SchemaRecordForm, {
        heading: "Vanguard",
        fields: sourceRecordFields("classes"),
        modelValue: model.value,
        referenceChoices: { weapon_type: [] },
        onSubmit
      })
    });
    app.mount(host);

    const name = host.querySelector<HTMLInputElement>("#field-name")!;
    name.value = "Guardian";
    name.dispatchEvent(new Event("input", { bubbles: true }));
    // A refresh elsewhere (creating a related record) hands the form a fresh
    // object with identical content; it must not clobber the pending edit.
    model.value = record();
    await nextTick();
    host.querySelector("form")!.dispatchEvent(
      new Event("submit", { bubbles: true, cancelable: true })
    );
    await nextTick();
    expect(onSubmit.mock.calls[0]?.[0].name).toBe("Guardian");

    // A record that genuinely changed elsewhere does replace the draft.
    model.value = { ...record(), name: "Renamed" };
    await nextTick();
    expect(host.querySelector<HTMLInputElement>("#field-name")!.value)
      .toBe("Renamed");
    app.unmount();
  });

  it("keeps a pending edit when a sibling save changes another field", async () => {
    const record = () => ({
      id: "vanguard",
      name: "Vanguard",
      baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
    });
    const model = ref<Record<string, unknown>>(record());
    const host = document.createElement("div");
    document.body.append(host);
    const onSubmit = vi.fn();
    const app = createApp({
      setup: () => () => h(SchemaRecordForm, {
        heading: "Vanguard",
        fields: sourceRecordFields("classes"),
        modelValue: model.value,
        referenceChoices: { weapon_type: [] },
        onSubmit
      })
    });
    app.mount(host);

    const name = host.querySelector<HTMLInputElement>("#field-name")!;
    name.value = "Guardian";
    name.dispatchEvent(new Event("input", { bubbles: true }));
    // A sibling editor saved a different field of this same record. The
    // incoming change lands; the pending edit to an untouched field survives.
    model.value = {
      ...record(),
      baseStats: { health: 12, movement: 4, strength: 3, defense: 2 }
    };
    await nextTick();

    expect(host.querySelector<HTMLInputElement>("#field-name")!.value)
      .toBe("Guardian");
    expect(host.querySelector<HTMLInputElement>("#field-baseStats-health")!.value)
      .toBe("12");
    app.unmount();
  });

  it("announces typed-but-unsubmitted edits as dirty", async () => {
    const host = document.createElement("div");
    document.body.append(host);
    const onDirty = vi.fn();
    const app = createApp(SchemaRecordForm, {
      heading: "Vanguard",
      fields: sourceRecordFields("classes"),
      modelValue: {
        id: "vanguard",
        name: "Vanguard",
        baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
      },
      referenceChoices: { weapon_type: [] },
      onDirty
    });
    app.mount(host);
    const name = host.querySelector<HTMLInputElement>("#field-name")!;
    name.value = "Guardian";
    name.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    expect(onDirty).toHaveBeenCalled();
    app.unmount();
  });

  it("renders identifier patterns the browser's v-flag regex accepts", () => {
    const { app, host } = mount({
      heading: "Vanguard",
      fields: sourceRecordFields("classes"),
      modelValue: {
        id: "vanguard",
        name: "Vanguard",
        baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
      },
      referenceChoices: { weapon_type: [] }
    });
    const pattern = host.querySelector<HTMLInputElement>("#field-id")!
      .getAttribute("pattern");
    // An attribute that fails to compile disables validation silently.
    expect(pattern).not.toBeNull();
    expect(() => new RegExp(pattern!, "v")).not.toThrow();
    expect(new RegExp(pattern!, "v").test("gate_house.a-b")).toBe(true);
    app.unmount();
  });

  it("holds a cleared required field back from every road that commits",
    async () => {
      // The `required` attribute is derived from the schema, and unless the
      // form checks it itself the attribute is decoration. `flush`, which is
      // what Save, Play and every section change call, raises no submit
      // event, so nothing in the browser would ever run it.
      const { app, host, onSubmit, form } = mount({
        heading: "Vanguard",
        fields: sourceRecordFields("classes"),
        modelValue: {
          id: "vanguard",
          name: "Vanguard",
          baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
        }
      });
      const name = host.querySelector<HTMLInputElement>("#field-name")!;
      name.value = "";
      name.dispatchEvent(new Event("input", { bubbles: true }));
      await nextTick();

      expect(form.flush()).toBe(false);
      expect(onSubmit).not.toHaveBeenCalled();
      host.querySelector("form")!.dispatchEvent(
        new Event("submit", { bubbles: true, cancelable: true })
      );
      await nextTick();
      expect(onSubmit).not.toHaveBeenCalled();
      expect(host.textContent).toContain("Nothing was saved.");
      app.unmount();
    });

  it("holds back an identifier the schema's pattern refuses", async () => {
    const { app, host, onSubmit, form } = mount({
      heading: "Vanguard",
      fields: sourceRecordFields("classes"),
      modelValue: {
        id: "vanguard",
        name: "Vanguard",
        baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
      }
    });
    const id = host.querySelector<HTMLInputElement>("#field-id")!;
    id.value = "Vanguard Of The Line";
    id.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();

    expect(form.flush()).toBe(false);
    expect(onSubmit).not.toHaveBeenCalled();
    app.unmount();
  });

  it("folds expert-only fields behind Advanced and leads with the rest", () => {
    const { app, host } = mount({
      heading: "Vanguard",
      fields: sourceRecordFields("classes"),
      modelValue: {
        id: "vanguard",
        name: "Vanguard",
        baseStats: { health: 10, movement: 4, strength: 3, defense: 2 }
      },
      referenceChoices: { weapon_type: [] }
    });
    const fold = host.querySelector<HTMLDetailsElement>("details.advanced-fields")!;
    expect(fold).not.toBeNull();
    // Identifiers and machine bookkeeping live behind the closed fold; the
    // fields that change how the game plays stay in front of it.
    expect(fold.hasAttribute("open")).toBe(false);
    expect(fold.querySelector("#field-id")).not.toBeNull();
    // Neither slot the compiler refuses is behind the fold, because neither is
    // offered at all: a control whose every use is a refusal is not "advanced".
    expect(fold.querySelector("#field-extensions")).toBeNull();
    expect(fold.querySelector("#field-scriptBindings")).toBeNull();
    expect(fold.querySelector("#field-name")).toBeNull();
    expect(host.querySelector(".field-group #field-name")).not.toBeNull();
    expect(host.querySelector(".field-group #field-baseStats-health"))
      .not.toBeNull();
    app.unmount();
  });

  // The fold's "a problem behind me must not hide behind me" rule is not
  // testable here. jsdom answers `checkValidity()` correctly but never matches
  // `:invalid`, which is the selector the rule is built on and verified
  // directly, and the other road into it, a JSON parse error, is gone with the two
  // fields the compiler refused. It wants a browser test, where `:invalid`
  // works; asserting it in jsdom would only ever assert jsdom.
});

describe("the targeting grids on the record form", () => {
  const cell = (host: HTMLElement, selector: string, dx: number, dy: number) =>
    host.querySelector<HTMLButtonElement>(`${selector} [data-cell="${dx}:${dy}"]`)!;

  const press = (button: HTMLButtonElement) =>
    button.dispatchEvent(
      new MouseEvent("pointerdown", { bubbles: true, button: 0 })
    );

  const band = ".targeting-preview section:first-child";
  const area = ".targeting-preview section:last-child";

  it("draws a weapon's reach band and nothing else", async () => {
    const { app, host } = mount({
      heading: "Long bow",
      fields: sourceRecordFields("weapons"),
      modelValue: {
        id: "long_bow", name: "Long bow", power: 4,
        minimumRange: 2, maximumRange: 3
      },
      referenceChoices: { weapon_type: [] }
    });
    await nextTick();
    // A weapon has a band and no area, so exactly one grid is drawn.
    expect(host.querySelectorAll(".targeting-grid")).toHaveLength(1);
    expect(host.querySelector(".targeting-grid h4")?.textContent)
      .toBe("Reach band");
    expect(cell(host, band, 1, 0).dataset.state).toBe("hole");
    expect(cell(host, band, 2, 0).dataset.state).toBe("covered");
    app.unmount();
  });

  it("draws both grids on an ability and composes them", async () => {
    const { app, host } = mount({
      heading: "Firestorm",
      fields: sourceRecordFields("abilities"),
      modelValue: {
        id: "firestorm", name: "Firestorm", kind: "damage", power: 6,
        minimumRange: 2, maximumRange: 4, areaShape: "diamond", radius: 2
      }
    });
    await nextTick();
    expect([...host.querySelectorAll(".targeting-grid h4")]
      .map((heading) => heading.textContent))
      .toEqual(["Reach band", "Area of impact"]);
    // The band grid offers the composed view because the record has both.
    expect(host.querySelectorAll(`${band} .targeting-modes input`))
      .toHaveLength(2);
    expect(cell(host, area, 2, 0).dataset.state).toBe("covered");
    expect(cell(host, area, 2, 2).dataset.state).toBe("outside");
    app.unmount();
  });

  it("draws no grid on a record carrying neither shape", async () => {
    const { app, host } = mount({
      heading: "Vanguard",
      fields: sourceRecordFields("classes"),
      modelValue: { id: "vanguard", name: "Vanguard" },
      referenceChoices: { weapon_type: [] }
    });
    await nextTick();
    expect(host.querySelector(".targeting-preview")).toBeNull();
    app.unmount();
  });

  it("writes the painted band into the record the form submits", async () => {
    const { app, host, onSubmit } = mount({
      heading: "Long bow",
      fields: sourceRecordFields("weapons"),
      modelValue: {
        id: "long_bow", name: "Long bow", power: 4,
        minimumRange: 1, maximumRange: 4
      },
      referenceChoices: { weapon_type: [] }
    });
    await nextTick();
    press(cell(host, band, 3, 0));
    await nextTick();
    // The generated number controls show the painted value: one editable
    // value reached two ways, not two values that can disagree.
    expect(host.querySelector<HTMLInputElement>("#field-minimumRange")!.value)
      .toBe("3");
    expect(host.querySelector<HTMLInputElement>("#field-maximumRange")!.value)
      .toBe("3");
    host.querySelector("form")!.dispatchEvent(
      new Event("submit", { bubbles: true, cancelable: true })
    );
    await nextTick();
    expect(onSubmit.mock.calls[0]?.[0]).toEqual(expect.objectContaining({
      minimumRange: 3, maximumRange: 3
    }));
    app.unmount();
  });

  it("follows the number controls when they are typed into", async () => {
    const { app, host } = mount({
      heading: "Long bow",
      fields: sourceRecordFields("weapons"),
      modelValue: {
        id: "long_bow", name: "Long bow", power: 4,
        minimumRange: 1, maximumRange: 2
      },
      referenceChoices: { weapon_type: [] }
    });
    await nextTick();
    expect(cell(host, band, 1, 0).dataset.state).toBe("covered");
    const minimum = host.querySelector<HTMLInputElement>("#field-minimumRange")!;
    minimum.value = "2";
    minimum.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    // Typing a minimum reach of two opens the hole, with no grid interaction.
    expect(cell(host, band, 1, 0).dataset.state).toBe("hole");
    app.unmount();
  });

  it("clears a radius the stored shape does not read", async () => {
    const { app, host, onSubmit } = mount({
      heading: "Firestorm",
      fields: sourceRecordFields("abilities"),
      modelValue: {
        id: "firestorm", name: "Firestorm", kind: "damage", power: 6,
        minimumRange: 1, maximumRange: 4, areaShape: "diamond", radius: 3
      }
    });
    await nextTick();
    press(cell(host, area, 1, 0));
    await nextTick();
    host.querySelector("form")!.dispatchEvent(
      new Event("submit", { bubbles: true, cancelable: true })
    );
    await nextTick();
    const submitted = onSubmit.mock.calls[0]?.[0];
    expect(submitted.areaShape).toBe("cross");
    // A cross ignores the radius in the engine, so leaving 3 on the record
    // would be a number nothing reads that contradicts the drawn shape.
    expect(submitted.radius).toBeUndefined();
    app.unmount();
  });
});
