// SPDX-License-Identifier: MIT
import { createApp, h, nextTick, reactive } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { CampaignItemGrant } from "../generated/source-v1";
import ItemGrantEditor from "./ItemGrantEditor.vue";

afterEach(() => document.body.replaceChildren());

const items = [
  { id: "tonic", name: "Field Tonic" },
  { id: "torch", name: "Pitch Torch" },
  { id: "rope", name: "Coil of Rope" }
];

function mount(
  grants: readonly CampaignItemGrant[],
  offered: readonly { id: string; name: string }[] = items
) {
  const host = document.createElement("div");
  document.body.append(host);
  const onUpdate = vi.fn();
  const onCreateItem = vi.fn();
  const app = createApp(ItemGrantEditor, {
    grants,
    items: offered,
    idPrefix: "stock",
    heading: "What the store starts with",
    help: "Leave it empty and the campaign begins with nothing.",
    grantWord: "starting stock",
    onUpdate,
    onCreateItem
  });
  app.mount(host);
  return { app, host, onUpdate, onCreateItem };
}

/**
 * The same editor under a parent that applies `update` back into props, the way
 * ContentWorkspace does. The single-step cases above do not need it; a gesture
 * made twice does, because the second press has to see the first one's result.
 */
function mountLive(
  grants: CampaignItemGrant[] = [],
  offered: readonly { id: string; name: string }[] = items
) {
  const host = document.createElement("div");
  document.body.append(host);
  const state = reactive({ grants });
  const app = createApp({
    setup() {
      return () => h(ItemGrantEditor, {
        grants: state.grants,
        items: offered,
        idPrefix: "stock",
        heading: "What the store starts with",
        help: "Leave it empty and the campaign begins with nothing.",
        grantWord: "starting stock",
        onUpdate: (next: CampaignItemGrant[]) => {
          state.grants = next;
        }
      });
    }
  });
  app.mount(host);
  return { app, host, state };
}

function button(host: HTMLElement, text: string): HTMLButtonElement {
  const found = [...host.querySelectorAll("button")].find(
    (candidate) => candidate.textContent?.trim().startsWith(text)
  );
  if (!found) throw new Error(`button '${text}' not found`);
  return found;
}

function lastGrants(onUpdate: ReturnType<typeof vi.fn>): CampaignItemGrant[] {
  return onUpdate.mock.lastCall?.[0] as CampaignItemGrant[];
}

describe("ItemGrantEditor", () => {
  it("gives every control a label a keyboard can reach it by", () => {
    const { app, host } = mount([{ itemId: "tonic", quantity: 2 }]);
    // Nothing here is a click target with no name: every input is a real
    // control with a real label, which is what makes the list usable by tab
    // and type alone.
    for (const id of [
      "stock-item-search",
      "stock-0-item",
      "stock-0-quantity",
      "stock-0-notes"
    ]) {
      const control = host.querySelector(`#${id}`);
      expect(control, id).not.toBeNull();
      expect(host.querySelector(`label[for="${id}"]`), id).not.toBeNull();
    }
    app.unmount();
  });

  it("adds a stock of the first item, one of it, rather than an empty form", () => {
    const { app, host, onUpdate } = mount([]);
    expect(host.textContent).toContain("Nothing here yet.");
    button(host, "Add starting stock").click();
    expect(lastGrants(onUpdate)).toEqual([{ itemId: "tonic", quantity: 1 }]);
    app.unmount();
  });

  it("narrows the choices by search without hiding the one already chosen", async () => {
    const { app, host } = mount([{ itemId: "tonic", quantity: 1 }]);
    const chooser = host.querySelector<HTMLSelectElement>("#stock-0-item")!;
    expect([...chooser.options].map((option) => option.value))
      .toEqual(["tonic", "torch", "rope"]);

    const search = host.querySelector<HTMLInputElement>("#stock-item-search")!;
    search.value = "rope";
    search.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    // The search narrows the list, and the grant's own choice survives it: a
    // search that could hide what is already chosen would silently offer to
    // change it.
    expect([...chooser.options].map((option) => option.value))
      .toEqual(["tonic", "rope"]);

    search.value = "nothing at all";
    search.dispatchEvent(new Event("input", { bubbles: true }));
    await nextTick();
    expect([...chooser.options].map((option) => option.value)).toEqual(["tonic"]);
    app.unmount();
  });

  it("names an item this project does not hold rather than dropping it", () => {
    const { app, host } = mount([{ itemId: "ghost", quantity: 1 }]);
    const chooser = host.querySelector<HTMLSelectElement>("#stock-0-item")!;
    expect(chooser.value).toBe("ghost");
    expect(host.textContent).toContain("not an item in this project");
    expect(host.textContent).toContain(
      "'ghost' is not an item in this project"
    );
    app.unmount();
  });

  it("explains a missing choice, a quantity that is not one, and a repeat", () => {
    const { app, host } = mount([
      { itemId: "", quantity: 1 },
      { itemId: "torch", quantity: 0 },
      { itemId: "rope", quantity: 70000 },
      { itemId: "rope", quantity: 1 }
    ]);
    const text = host.textContent!;
    expect(text).toContain("Choose which item this puts in the store.");
    expect(text).toContain("Say how many, as a whole number of at least 1.");
    expect(text).toContain("65535 is the most of one item");
    // Two entries for one item are two different answers to one question.
    expect(text).toContain("This list already stocks 'rope'");
    app.unmount();
  });

  it("edits and removes one entry without disturbing the others", async () => {
    const { app, host, onUpdate } = mount([
      { itemId: "tonic", quantity: 1 },
      { itemId: "torch", quantity: 4, notes: "for the tunnels" }
    ]);
    const quantity = host.querySelector<HTMLInputElement>("#stock-0-quantity")!;
    quantity.value = "9";
    quantity.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    expect(lastGrants(onUpdate)).toEqual([
      { itemId: "tonic", quantity: 9 },
      { itemId: "torch", quantity: 4, notes: "for the tunnels" }
    ]);

    // An emptied note is removed rather than blanked: an absent field says
    // something different from a field holding nothing.
    const notes = host.querySelector<HTMLTextAreaElement>("#stock-1-notes")!;
    notes.value = "   ";
    notes.dispatchEvent(new Event("change", { bubbles: true }));
    await nextTick();
    expect(lastGrants(onUpdate)[1]).not.toHaveProperty("notes");

    button(host, "Remove starting stock 1").click();
    expect(lastGrants(onUpdate)).toEqual([
      { itemId: "torch", quantity: 4, notes: "for the tunnels" }
    ]);
    app.unmount();
  });

  it("says there is nothing to give, and offers the edit that fixes it", () => {
    const { app, host, onCreateItem } = mount([], []);
    expect(host.textContent).toContain("This project has no items yet");
    expect(host.querySelector("#stock-item-search")).toBeNull();
    button(host, "Create related item").click();
    expect(onCreateItem).toHaveBeenCalled();
    app.unmount();
  });

  it("keeps focus while a quantity is typed digit by digit", async () => {
    const { app, host } = mount([{ itemId: "tonic", quantity: 1 }]);
    const quantity = host.querySelector<HTMLInputElement>("#stock-0-quantity")!;
    quantity.focus();
    for (const partial of ["1", "12", "123"]) {
      quantity.value = partial;
      quantity.dispatchEvent(new Event("input", { bubbles: true }));
      await nextTick();
    }
    // The fieldset must not remount as the entry is edited: a key that changed
    // with the choice would throw the author's focus away mid-number.
    expect(host.querySelector("#stock-0-quantity")).toBe(quantity);
    expect(document.activeElement).toBe(quantity);
    expect(quantity.value).toBe("123");
    app.unmount();
  });

  it("cannot stock the same item twice, however the author presses", async () => {
    // The fault this prevents was met in a real project: a store naming one
    // item twice, refused by the format and reported by a validator after the
    // fact. Pressing add twice was all it took, because a fresh grant used to
    // start as the *first item in the project* rather than the first one this
    // list had not already taken.
    const { app, host, state } = mountLive();

    button(host, "Add starting stock").click();
    await nextTick();
    button(host, "Add starting stock").click();
    await nextTick();

    const stocked = state.grants.map((grant) => grant.itemId);
    expect(stocked).toHaveLength(2);
    expect(new Set(stocked).size).toBe(2);
    app.unmount();
  });

  it("offers no item another row has already taken, but keeps its own",
    async () => {
      const { app, host, state } = mountLive();
      button(host, "Add starting stock").click();
      await nextTick();
      button(host, "Add starting stock").click();
      await nextTick();

      const first = host.querySelector<HTMLSelectElement>(
        "#stock-0-item"
      )!;
      const offered = [...first.options].map((option) => option.value);
      // Its own choice survives: a menu its own value is missing from is a row
      // that looks as though it changed by itself.
      expect(offered).toContain(state.grants[0]!.itemId);
      expect(offered).not.toContain(state.grants[1]!.itemId);
      app.unmount();
    });

  it("stops offering to add once every item is stocked", async () => {
    const { app, host, state } = mountLive();
    const add = () => button(host, "Add starting stock");
    for (let i = 0; i < 20 && !add().disabled; ++i) {
      add().click();
      await nextTick();
    }
    // Every item, once each, and then the gesture withdraws rather than
    // offering a press whose only outcome is a list the format refuses.
    expect(new Set(state.grants.map((g) => g.itemId)).size)
      .toBe(state.grants.length);
    expect(add().disabled).toBe(true);
    expect(host.textContent).toContain("already stocked here");
    app.unmount();
  });
});
