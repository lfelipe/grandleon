// SPDX-License-Identifier: MIT
import { createApp, nextTick } from "vue";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { SourceFieldDescriptor } from "../domain/source-form-model";
import SchemaRecordForm from "./SchemaRecordForm.vue";

afterEach(() => document.body.replaceChildren());

describe("SchemaRecordForm script-binding security boundary", () => {
  it("renders and submits hostile parameter text as inert data", async () => {
    const hostile =
      '<img src=x onerror="globalThis.bindingExecuted=true">' +
      "<script>globalThis.bindingExecuted=true</script>";
    const fields: readonly SourceFieldDescriptor[] = [{
      path: ["parameters", "caption"],
      label: "Caption",
      kind: "text",
      required: true
    }];
    const host = document.createElement("div");
    document.body.append(host);
    const onSubmit = vi.fn();
    const app = createApp(SchemaRecordForm, {
      heading: "Script binding",
      fields,
      modelValue: { parameters: { caption: hostile } },
      onSubmit
    });

    app.mount(host);

    const input = host.querySelector<HTMLInputElement>(
      "#field-parameters-caption"
    );
    expect(input?.value).toBe(hostile);
    expect(host.querySelector("img")).toBeNull();
    expect(host.querySelector("script")).toBeNull();

    host.querySelector("form")?.dispatchEvent(
      new Event("submit", { bubbles: true, cancelable: true })
    );
    await nextTick();

    expect(onSubmit).toHaveBeenCalledWith({
      parameters: { caption: hostile }
    });
    app.unmount();
  });
});
