<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
import { computed, ref, watch } from "vue";
import {
  advancedFieldsNote,
  htmlPattern,
  type SourceFieldDescriptor
} from "../domain/source-form-model";
import TargetingShapeGrid from "./TargetingShapeGrid.vue";
import { areaShapes, type AreaShape } from "../domain/targeting-geometry";

export interface ReferenceChoice {
  readonly id: string;
  readonly name: string;
  readonly compatibility?: "compatible" | "incompatible" | "unknown";
  readonly explanation?: string;
}

const props = defineProps<{
  heading: string;
  fields: readonly SourceFieldDescriptor[];
  modelValue: Readonly<Record<string, unknown>>;
  referenceChoices?: Readonly<Record<string, readonly ReferenceChoice[]>>;
  readonlyPaths?: readonly string[];
  /**
   * Whether something on the page already names this form.
   *
   * A form standing under a heading that names it said the name twice:
   * "Game settings" above "game settings", "Testing" above "testing aids". A
   * class that only moved the second one off screen fixed half of it: the
   * heading stayed in the document outline, so a screen reader still read both
   * and the duplicate was audible where it was no longer visible.
   *
   * So the heading is not drawn at all. The words are not lost: they name the
   * form itself, which makes it a landmark a screen reader can jump to, and
   * they still name the submit button ("Save game settings"). What goes is one
   * entry in the outline, which is exactly the thing that was doubled.
   */
  headingAlreadyGiven?: boolean;
}>();

const emit = defineEmits<{
  submit: [record: Record<string, unknown>];
  createRelated: [category: NonNullable<SourceFieldDescriptor["referenceCategory"]>];
  dirty: [];
}>();

function copyRecord(value: Readonly<Record<string, unknown>>): Record<string, unknown> {
  return JSON.parse(JSON.stringify(value)) as Record<string, unknown>;
}

const draft = ref<Record<string, unknown>>(copyRecord(props.modelValue));
const search = ref<Record<string, string>>({});
const fieldErrors = ref<Record<string, string>>({});

/**
 * What is on screen while a structured field is being typed.
 *
 * The fields whose stored value is not a string need this, and they need it
 * badly. A JSON field's words are made by re-serializing an object: a draft
 * holding `{"a":1}` renders as three pretty-printed lines, which is not what
 * the author typed, and re-applying it on the next redraw would move the cursor
 * mid-word. Half-typed JSON has no stored form at all. A list of values, one
 * per line, is the same shape of problem: the empty line an author has just
 * opened to write the next value is trimmed away by the very rule that reads
 * the control, so the newline would vanish as it was struck.
 *
 * So the raw text is kept exactly as struck until the field is left, and only
 * then does the value the record holds take the drawing back.
 */
const typedText = ref<Record<string, string>>({});

// Which fields stand behind the fold is decided by `source-form-model.ts`,
// where the schema and the words for it already live, and not here: this form
// draws whatever list it is handed and a second opinion about what counts as
// advanced would be a second answer to one question. They stay fully editable,
// and the form leads with the ones a first game actually answers.
const basicFields = computed(() =>
  props.fields.filter((field) => field.advanced !== true)
);

/**
 * What the fold holds, which is nothing at all when there is no plainer field
 * for the form to lead with.
 *
 * A fold is a way of leading with one thing and keeping another behind it, so
 * a form whose every field is advanced has no use for one: it would open on a
 * heading, a closed triangle and no content, and every road to its controls
 * would run through a door saying most games never need what is inside. The
 * project's own file is that form, a version, a package number and a notes
 * box, and it is already somewhere an author went on purpose.
 */
const advancedFields = computed(() =>
  basicFields.value.length === 0
    ? []
    : props.fields.filter((field) => field.advanced === true)
);

const fieldGroups = computed(() => [
  {
    id: "basic",
    advanced: false,
    fields: advancedFields.value.length === 0 ? props.fields : basicFields.value
  },
  ...(advancedFields.value.length > 0
    ? [{ id: "advanced", advanced: true, fields: advancedFields.value }]
    : [])
]);

/** What is behind the fold, named, never a description that outlived it. */
const advancedNote = computed(() => advancedFieldsNote(advancedFields.value));

const advancedOpen = ref(false);

// A problem behind the fold must never hide there; reveal it. The fold is
// only ever forced open, so an author's own toggle is otherwise respected.
watch(
  fieldErrors,
  (errors) => {
    const hidden = new Set(
      advancedFields.value.map((field) => field.path.join("."))
    );
    if (Object.keys(errors).some((key) => hidden.has(key))) {
      advancedOpen.value = true;
    }
  },
  { deep: true }
);

/** The plain word for a reference category, in the editor's vocabulary. */
function categoryWord(
  category: NonNullable<SourceFieldDescriptor["referenceCategory"]>
): string {
  return category === "dialogue" ? "scene" : category.replace("_", " ");
}

// Saving, or creating a related record, refreshes the project and hands this
// form a fresh object with the same content. Resetting the draft on that
// identity churn would silently discard unsaved edits, so only a record that
// actually differs from the one last loaded replaces the draft.
let loadedRecord = JSON.stringify(props.modelValue);

watch(
  () => props.modelValue,
  (value) => {
    const serialized = JSON.stringify(value);
    if (serialized === loadedRecord) return;
    // A sibling editor saving this record (the flow editor writing a
    // campaign's flow, an inline scene edit) must not wipe pending edits to
    // unrelated fields: each edited field survives unless the incoming record
    // actually changed that same field.
    const previous = JSON.parse(loadedRecord) as Record<string, unknown>;
    const incoming = copyRecord(value);
    const keys = new Set([
      ...Object.keys(draft.value),
      ...Object.keys(previous)
    ]);
    for (const key of keys) {
      const edited =
        JSON.stringify(draft.value[key]) !== JSON.stringify(previous[key]);
      const touched =
        JSON.stringify(incoming[key]) !== JSON.stringify(previous[key]);
      if (edited && !touched) {
        if (draft.value[key] === undefined) delete incoming[key];
        else incoming[key] = JSON.parse(JSON.stringify(draft.value[key]));
      } else if (touched) {
        // The incoming record won this field, so raw text still being drawn for
        // it is stale and would commit over the answer on the way out.
        for (const path of Object.keys(typedText.value)) {
          if (path === key || path.startsWith(`${key}.`)) {
            delete typedText.value[path];
          }
        }
      }
    }
    loadedRecord = serialized;
    draft.value = incoming;
  },
  { deep: true }
);

// An unsaved form draft is real work; the project header and the
// close-the-tab guard must know about it.
watch(
  draft,
  (value) => {
    if (JSON.stringify(value) !== loadedRecord) emit("dirty");
  },
  { deep: true }
);

function controlId(field: SourceFieldDescriptor): string {
  return `field-${field.path.join("-")}`;
}

function getValue(path: readonly string[]): unknown {
  return path.reduce<unknown>((value, segment) =>
    value && typeof value === "object"
      ? (value as Record<string, unknown>)[segment]
      : undefined, draft.value);
}

function setValue(path: readonly string[], value: unknown) {
  let target = draft.value;
  for (const segment of path.slice(0, -1)) {
    const child = target[segment];
    if (!child || typeof child !== "object" || Array.isArray(child)) {
      target[segment] = {};
    }
    target = target[segment] as Record<string, unknown>;
  }
  const name = path.at(-1);
  if (!name) return;
  if (value === undefined) delete target[name];
  else target[name] = value;
}

function textValue(field: SourceFieldDescriptor): string {
  const key = field.path.join(".");
  const value = getValue(field.path);
  if (field.kind === "json") {
    const typed = typedText.value[key];
    if (typed !== undefined) return typed;
    return value === undefined ? "" : JSON.stringify(value, null, 2);
  }
  return value === undefined ? "" : String(value);
}

/** The same, for the list a `string-list` control holds one value per line. */
function listText(field: SourceFieldDescriptor): string {
  const typed = typedText.value[field.path.join(".")];
  if (typed !== undefined) return typed;
  return (getValue(field.path) as string[] | undefined)?.join("\n") ?? "";
}

function updateList(field: SourceFieldDescriptor, event: Event) {
  const raw = (event.target as HTMLTextAreaElement).value;
  typedText.value[field.path.join(".")] = raw;
  setValue(
    field.path,
    raw.split("\n").map((value) => value.trim()).filter(Boolean)
  );
}

function leaveList(field: SourceFieldDescriptor, event: Event) {
  updateList(field, event);
  delete typedText.value[field.path.join(".")];
}

function updateText(field: SourceFieldDescriptor, event: Event) {
  const value = (event.target as HTMLInputElement | HTMLTextAreaElement).value;
  const key = field.path.join(".");
  delete fieldErrors.value[key];
  if (field.kind === "json") typedText.value[key] = value;
  if (!field.required && value === "") {
    setValue(field.path, undefined);
  } else if (field.kind === "integer") {
    setValue(field.path, Number(value));
  } else if (field.kind === "json") {
    try {
      setValue(field.path, value === "" ? undefined : JSON.parse(value));
    } catch (error) {
      fieldErrors.value[key] =
        error instanceof Error ? error.message : "Invalid JSON";
      // Words that cannot be parsed are still work in progress, and the header
      // is where an author reads that there is some. The draft watcher below
      // cannot say so, because nothing reached the draft.
      emit("dirty");
    }
  } else {
    setValue(field.path, value);
  }
}

/**
 * Leaving a structured field, at which point what the record holds takes the
 * drawing back. Unparseable text is kept on screen rather than replaced by the
 * last thing that parsed: the problem is named beside the control and the words
 * that caused it have to stay readable to be fixable.
 */
function leaveText(field: SourceFieldDescriptor, event: Event) {
  updateText(field, event);
  const key = field.path.join(".");
  if (!fieldErrors.value[key]) delete typedText.value[key];
}

function choices(field: SourceFieldDescriptor): readonly ReferenceChoice[] {
  const all = props.referenceChoices?.[field.referenceCategory ?? ""] ?? [];
  const query = (search.value[field.path.join(".")] ?? "").trim().toLocaleLowerCase();
  return query
    ? all.filter((choice) =>
      choice.id.toLocaleLowerCase().includes(query) ||
      choice.name.toLocaleLowerCase().includes(query)
    )
    : all;
}

function missingReference(field: SourceFieldDescriptor): string | undefined {
  const value = getValue(field.path);
  if (typeof value !== "string" || value === "") return undefined;
  return (props.referenceChoices?.[field.referenceCategory ?? ""] ?? [])
    .some((choice) => choice.id === value) ? undefined : value;
}

function selected(field: SourceFieldDescriptor, id: string): boolean {
  const value = getValue(field.path);
  return Array.isArray(value) && value.includes(id);
}

function toggleReference(field: SourceFieldDescriptor, id: string) {
  const current = getValue(field.path);
  const values = Array.isArray(current) ? [...current] : [];
  const index = values.indexOf(id);
  if (index >= 0) values.splice(index, 1);
  else values.push(id);
  setValue(field.path, values);
}

function enableOptionalList(field: SourceFieldDescriptor, enabled: boolean) {
  setValue(field.path, enabled ? [] : undefined);
}

function updateBoolean(field: SourceFieldDescriptor, checked: boolean) {
  // An optional boolean that is off reads best as an absent field: the
  // record stays identical to one that never mentioned it.
  setValue(field.path, checked ? true : field.required ? false : undefined);
}

function updateSelect(field: SourceFieldDescriptor, value: string) {
  setValue(field.path, value === "" ? undefined : value);
}

/** A stored select value the schema no longer offers, shown rather than lost. */
function unknownOption(field: SourceFieldDescriptor): string | undefined {
  const value = getValue(field.path);
  if (typeof value !== "string" || value === "") return undefined;
  return field.options?.some((option) => option.value === value)
    ? undefined
    : value;
}

const hasFields = computed(() => props.fields.length > 0);

// A reach band and an area of impact are shapes on a grid, and four separate
// number and menu controls leave an author to imagine them. The grids below
// draw what those controls store. They are shown only where the record actually
// carries the fields, they write nothing the controls could not, and the
// controls stay fully editable beside them.
const hasBandFields = computed(() =>
  props.fields.some((field) => field.path.join(".") === "minimumRange") &&
  props.fields.some((field) => field.path.join(".") === "maximumRange")
);

const hasAreaFields = computed(() =>
  props.fields.some((field) => field.path.join(".") === "areaShape")
);

function numberField(name: string): number | undefined {
  const value = getValue([name]);
  return typeof value === "number" ? value : undefined;
}

const storedAreaShape = computed<AreaShape | undefined>(() => {
  const value = getValue(["areaShape"]);
  return areaShapes.includes(value as AreaShape) ? (value as AreaShape) : undefined;
});

/**
 * What a strike aimed inside the band would cover, for the composed view. Only
 * a record carrying both shapes has one.
 */
const composedArea = computed(() =>
  hasAreaFields.value
    ? { shape: storedAreaShape.value ?? "single", radius: numberField("radius") }
    : undefined
);

function applyBand(fields: { minimumRange: number; maximumRange: number }) {
  setValue(["minimumRange"], fields.minimumRange);
  setValue(["maximumRange"], fields.maximumRange);
}

function applyArea(fields: { areaShape: AreaShape; radius: number }) {
  setValue(["areaShape"], fields.areaShape);
  // The radius is documented as read only by the diamond, and the engine's
  // `covered_by` is where that is true. Storing it anyway would leave a number
  // on the record that nothing reads and that contradicts the drawn shape, so
  // the shapes that ignore it clear it instead.
  if (fields.areaShape === "diamond") setValue(["radius"], fields.radius);
  else setValue(["radius"], undefined);
}

const formElement = ref<HTMLFormElement>();
/**
 * What the browser's own constraint validation refused, in the field's words.
 *
 * It is shown as well as reported, because the two roads into this form are not
 * the same: pressing Save shows the browser's bubble beside the control, while
 * a flush, on leaving the section, pressing Play or saving the project, happens
 * with the author's attention elsewhere and needs something that stays on
 * screen.
 */
const constraintProblem = ref("");

/** The words next to a control, for naming it in a refusal. */
function controlLabel(control: Element): string {
  const named = control.id
    ? formElement.value?.querySelector(`label[for="${CSS.escape(control.id)}"]`)
    : undefined;
  return named?.textContent?.trim().replace(/\s*\*$/, "") ?? "This field";
}

/**
 * Whether every control in this form holds something the schema admits.
 *
 * The `required`, `pattern`, `minlength` and range attributes on the controls
 * above are derived from the schema, and this is what makes them mean
 * something: the form is never submitted by a real submit event on some of the
 * roads that commit it, so a check that only the browser performed would be a
 * guard with two ways around it. The fold is opened first when the offending
 * control is behind it: a problem an author cannot see is a problem they
 * cannot fix.
 */
function constraintsMet(): boolean {
  const form = formElement.value;
  if (!form || typeof form.checkValidity !== "function") return true;
  if (form.checkValidity()) {
    constraintProblem.value = "";
    return true;
  }
  const invalid = form.querySelector<HTMLInputElement>(":invalid");
  if (invalid?.closest(".advanced-fields")) advancedOpen.value = true;
  constraintProblem.value = invalid
    ? `${controlLabel(invalid)}: ${
      invalid.validationMessage || "this value is not allowed here"}`
    : "Some of this record is not something the format can hold.";
  if (typeof form.reportValidity === "function") form.reportValidity();
  return false;
}

function submit() {
  if (Object.keys(fieldErrors.value).length > 0) return;
  if (!constraintsMet()) return;
  emit("submit", JSON.parse(JSON.stringify(draft.value)) as Record<string, unknown>);
}

/** Commits a pending draft; false when the record's own problems kept it out. */
function flush(): boolean {
  if (JSON.stringify(draft.value) === loadedRecord) return true;
  if (Object.keys(fieldErrors.value).length > 0) return false;
  if (!constraintsMet()) return false;
  submit();
  return true;
}

defineExpose({ flush });
</script>

<template>
  <form ref="formElement" class="schema-form" novalidate
    :aria-label="headingAlreadyGiven ? heading : undefined"
    @submit.prevent="submit">
    <h3 v-if="!headingAlreadyGiven">{{ heading }}</h3>
    <p v-if="!hasFields">This schema has no editable fields.</p>

    <template v-for="group in fieldGroups" :key="group.id">
    <component :is="group.advanced ? 'details' : 'div'"
      :class="group.advanced ? 'advanced-fields' : 'field-group'"
      :open="group.advanced && advancedOpen ? true : undefined"
      @toggle="group.advanced &&
        (advancedOpen = ($event.target as HTMLDetailsElement).open)">
      <summary v-if="group.advanced">Advanced</summary>
      <p v-if="group.advanced && advancedNote" class="field-help">
        {{ advancedNote }}
      </p>
    <div v-for="field in group.fields" :key="field.path.join('.')"
      class="schema-field">
      <template v-if="field.referenceCategory && field.kind === 'text'">
        <label :for="controlId(field)">
          {{ field.label }}<span v-if="field.required" aria-hidden="true"> *</span>
        </label>
        <label :for="`${controlId(field)}-search`">Search {{ field.label }}</label>
        <input :id="`${controlId(field)}-search`"
          v-model="search[field.path.join('.')]" type="search">
        <select :id="controlId(field)" :required="field.required"
          :value="getValue(field.path) ?? ''"
          @change="setValue(field.path,
            ($event.target as HTMLSelectElement).value || undefined)">
          <option value="">
            {{ field.required ? "Select a value" : "Not assigned" }}
          </option>
          <option v-if="missingReference(field)"
            :value="missingReference(field)" disabled>
            Missing reference: {{ missingReference(field) }}
          </option>
          <option v-for="choice in choices(field)" :key="choice.id"
            :value="choice.id" :disabled="choice.compatibility === 'incompatible'">
            {{ choice.name }} ({{ choice.id }}){{
              choice.compatibility === "unknown" ? ": compatibility unknown" : "" }}
          </option>
        </select>
        <p v-if="missingReference(field)" class="field-error" role="alert">
          The selected {{ categoryWord(field.referenceCategory) }}
          '{{ missingReference(field) }}' does not exist in this project.
        </p>
        <button type="button" class="secondary"
          @click="emit('createRelated', field.referenceCategory)">
          Create related {{ categoryWord(field.referenceCategory) }}
        </button>
        <p v-for="choice in choices(field).filter((item) => item.explanation)"
          :key="`explanation-${choice.id}`" class="field-help">
          {{ choice.name }}: {{ choice.explanation }}
        </p>
      </template>

      <fieldset v-else-if="field.referenceCategory && field.kind === 'string-list'">
        <legend>
          {{ field.label }}<span v-if="field.required" aria-hidden="true"> *</span>
        </legend>
        <label v-if="!field.required" class="optional-toggle">
          <input type="checkbox" :checked="getValue(field.path) !== undefined"
            @change="enableOptionalList(field,
              ($event.target as HTMLInputElement).checked)">
          Configure this field
        </label>
        <template v-if="field.required || getValue(field.path) !== undefined">
          <label :for="`${controlId(field)}-search`">Search {{ field.label }}</label>
          <input :id="`${controlId(field)}-search`"
            v-model="search[field.path.join('.')]" type="search">
          <div class="reference-choices">
            <label v-for="choice in choices(field)" :key="choice.id"
              :class="{ incompatible: choice.compatibility === 'incompatible' }">
              <input type="checkbox" :checked="selected(field, choice.id)"
                :disabled="choice.compatibility === 'incompatible'"
                @change="toggleReference(field, choice.id)">
              {{ choice.name }} ({{ choice.id }})
              <small v-if="choice.explanation">{{ choice.explanation }}</small>
            </label>
          </div>
        </template>
      </fieldset>

      <template v-else-if="field.kind === 'textarea' || field.kind === 'json'">
        <label :for="controlId(field)">
          {{ field.label }}<span v-if="field.required" aria-hidden="true"> *</span>
        </label>
        <!-- `input` holds the keystroke and `change` commits it, exactly as the
             single-line control below does. Both, because they answer different
             questions: whether the editor knows there is work in progress, and
             when the words the record holds take the drawing back. -->
        <textarea :id="controlId(field)" :required="field.required"
          :value="textValue(field)"
          :aria-invalid="fieldErrors[field.path.join('.')] ? 'true' : undefined"
          :aria-describedby="fieldErrors[field.path.join('.')]
            ? `${controlId(field)}-error`
            : undefined"
          @input="updateText(field, $event)"
          @change="leaveText(field, $event)" />
        <p v-if="fieldErrors[field.path.join('.')]" :id="`${controlId(field)}-error`"
          class="field-error" role="alert">
          {{ fieldErrors[field.path.join(".")] }}
        </p>
      </template>

      <template v-else-if="field.kind === 'boolean'">
        <label class="boolean-field" :for="controlId(field)">
          <input :id="controlId(field)" type="checkbox"
            :checked="getValue(field.path) === true"
            @change="updateBoolean(field,
              ($event.target as HTMLInputElement).checked)">
          {{ field.label }}
        </label>
      </template>

      <template v-else-if="field.kind === 'select'">
        <label :for="controlId(field)">
          {{ field.label }}<span v-if="field.required" aria-hidden="true"> *</span>
        </label>
        <select :id="controlId(field)" :required="field.required"
          :value="getValue(field.path) ?? ''"
          @change="updateSelect(field,
            ($event.target as HTMLSelectElement).value)">
          <option v-if="!field.required" value="">
            {{ field.unsetLabel ?? "Not set" }}
          </option>
          <option v-else-if="getValue(field.path) === undefined" value="" disabled>
            Choose one
          </option>
          <option v-if="unknownOption(field)" :value="unknownOption(field)" disabled>
            {{ unknownOption(field) }}: not a recognised choice
          </option>
          <option v-for="option in field.options ?? []" :key="option.value"
            :value="option.value">
            {{ option.label }}
          </option>
        </select>
      </template>

      <template v-else-if="field.kind === 'string-list'">
        <label :for="controlId(field)">
          {{ field.label }} (one value per line)
        </label>
        <textarea :id="controlId(field)" :value="listText(field)"
          @input="updateList(field, $event)"
          @change="leaveList(field, $event)" />
      </template>

      <template v-else>
        <label :for="controlId(field)">
          {{ field.label }}<span v-if="field.required" aria-hidden="true"> *</span>
        </label>
        <input :id="controlId(field)"
          :type="field.kind === 'integer' ? 'number' : 'text'"
          :required="field.required" :min="field.minimum" :max="field.maximum"
          :readonly="readonlyPaths?.includes(field.path.join('.'))"
          :minlength="field.minLength" :maxlength="field.maxLength"
          :pattern="htmlPattern(field.pattern)" :value="textValue(field)"
          @input="updateText(field, $event)">
      </template>

      <p v-if="field.description" class="field-help">{{ field.description }}</p>
    </div>
    </component>
    </template>

    <div v-if="hasBandFields || hasAreaFields" class="targeting-preview">
      <TargetingShapeGrid v-if="hasBandFields" kind="band"
        :minimum-range="numberField('minimumRange')"
        :maximum-range="numberField('maximumRange')"
        :compose="composedArea"
        @band="applyBand" />
      <TargetingShapeGrid v-if="hasAreaFields" kind="area"
        :area-shape="storedAreaShape" :radius="numberField('radius')"
        @area="applyArea" />
    </div>

    <p v-if="constraintProblem" class="field-error" role="alert">
      {{ constraintProblem }} Nothing was saved.
    </p>
    <button type="submit">Save {{ heading }}</button>
  </form>
</template>
