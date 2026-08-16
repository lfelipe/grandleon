<!-- SPDX-License-Identifier: MIT -->
<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, shallowRef } from "vue";
import {
  createAnalysisWorkerClient,
  type AnalysisWorkerClient
} from "./analysis/analysis-worker-client";
import { analyzeSourceProject, type SourceAnalysis } from "./analysis/source-analysis";
import ContentWorkspace from "./components/ContentWorkspace.vue";
import type { PresentedDiagnostic } from "./components/DiagnosticPanel.vue";
import EditorErrorBoundary from "./components/EditorErrorBoundary.vue";
import PlayMode from "./components/PlayMode.vue";
import StartScreen from "./components/StartScreen.vue";
import { createSampleProject, sampleProjects } from "./sample-projects";
import {
  DEFAULT_WORKSPACE_SECTION,
  WORKSPACE_SECTIONS
} from "./domain/workspace-sections";
import { initEncounterEngine } from "./domain/encounter-simulation";
import { MemoryProjectStore } from "./domain/memory-project-store";
import { ProjectStoreError, type ProjectStore } from "./domain/project-store";
import {
  bringProjectUpToDate,
  createSourceProject,
  describeProblem,
  encodeSourceProject,
  SourceProjectDocument,
  sourceProjectPath,
  UnwritableProjectError,
  type OtherVersionSourceProject,
  type UnreadableSourceProject
} from "./domain/source-project-document";
import { targetNotes, type TargetNote } from "./domain/target-budget";
import type { SourceProject } from "./generated/source-v1";
import { IndexedDbProjectStore } from "./platform/indexeddb-project-store";
import {
  RomService,
  RomServiceError,
  type RomBuildStatus,
  type RomServiceHealth
} from "./platform/rom-service";

type AnalyzeProject = (sourcePath: string, text: string) => Promise<SourceAnalysis>;
type DownloadArchive = (
  bytes: Uint8Array,
  filename: string,
  contentType?: string
) => void;

const props = defineProps<{
  projectStore?: ProjectStore;
  analyzeProject?: AnalyzeProject;
  downloadArchive?: DownloadArchive;
  romService?: RomService;
}>();

function browserStore(): ProjectStore {
  return typeof indexedDB === "undefined"
    ? new MemoryProjectStore("browser-session")
    : new IndexedDbProjectStore("default");
}

function browserDownload(
  bytes: Uint8Array,
  filename: string,
  contentType = "application/zip"
) {
  const blob = new Blob([bytes as BlobPart], { type: contentType });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = filename;
  // The anchor has to be in the document for the click to start a download in
  // every browser, and the object URL has to outlive the click: revoking it in
  // the same task races the download and can hand the user an empty file.
  // Release it on the next macrotask instead, once the download has taken its
  // own reference to the blob.
  link.style.display = "none";
  document.body.append(link);
  link.click();
  setTimeout(() => {
    link.remove();
    URL.revokeObjectURL(url);
  }, 0);
}

// The sword from the project mark, derived from
// tools/placeholder_art/gallery/logo.png by
// editor/scripts/generate-logo-assets.py. Decoration beside the heading, so
// it carries no alternative text of its own.
const logoUrl = `${import.meta.env.BASE_URL}logo.png`;

const store = props.projectStore ?? browserStore();
const documentModel = new SourceProjectDocument(store);
const project = shallowRef<SourceProject>(createSourceProject());
const fileRevision = ref<number>();
const savedStatus = ref("Opening local draft…");
const dirty = ref(false);
const busy = ref(false);
const workspaceKey = ref(0);
const diagnostics = ref<readonly PresentedDiagnostic[]>([]);
// What an old console would make of the game as it stands. Recomputed on every
// edit rather than on Validate, because the point of it is to be there while a
// choice is still being made. Unlike validation it is a small synchronous
// count over data already in hand, so it costs nothing to keep current.
const consoleNotes = ref<readonly TargetNote[]>([]);
const importInput = ref<HTMLInputElement>();
const playing = ref(false);
const engineReady = ref(false);
const workspace = ref<InstanceType<typeof ContentWorkspace>>();
/**
 * Which of the two screens is up.
 *
 * It starts on the way in, and a recovered draft moves it to the workspace:
 * an author with work in progress asked for the work rather than for a menu.
 * The workspace is hidden rather than unmounted when the start screen returns,
 * because the workspace owns the editing session: the undo history, the
 * pending drafts and the record selection. A menu may not throw those
 * away.
 */
const view = ref<"start" | "workspace">("start");
/** Whether a project has ever been opened in this tab, so the start screen
 *  knows whether there is anything to go back to. */
const opened = ref(false);
/** The section the rail marks. Set from what the workspace reports, never from
 *  the click, so a refused departure cannot leave the rail lying. */
const activeSection = ref(DEFAULT_WORKSPACE_SECTION);
const sections = WORKSPACE_SECTIONS;
// A stored draft that no longer decodes. Its bytes are the author's only
// copy, so it is held here, downloadable and never overwritten, until the
// author explicitly lets it go.
const unreadableDraft = ref<UnreadableSourceProject>();
// A game made with another Grandleon, and what it was called, held until the
// author says whether to bring it up to date. Nothing has been opened and
// nothing has been written while this is set.
// Shallow, and that is load-bearing rather than an optimisation: a deep `ref`
// hands out a proxy of whatever it holds, and the game inside this is about to
// be copied by the migration registry, which copies structures and not proxies.
// Nothing here is ever edited in place either, the whole value being replaced
// or cleared, so there is nothing for deep reactivity to be for.
const otherVersion = shallowRef<{
  readonly opened: OtherVersionSourceProject;
  readonly named: string;
  /** True when these bytes are the stored draft rather than a chosen file. */
  readonly stored: boolean;
}>();
let analysisWorker: AnalysisWorkerClient | undefined;

/**
 * Hold a game from another Grandleon and show the author what it would take to
 * open it.
 *
 * The dialog it puts up is the whole of the editor's answer to a project that
 * is out of date: it names the Grandleon that made the game, the one it needs,
 * and every change bringing it up would make, one sentence each, in the order
 * they would happen. Cancel leaves the file alone; so does doing nothing.
 */
function askAboutVersion(
  opened: OtherVersionSourceProject,
  named: string,
  stored: boolean
) {
  otherVersion.value = { opened, named, stored };
  view.value = "start";
}

/**
 * The author declined. The game is not opened and nothing it came from is
 * rewritten.
 *
 * A stored game gets one thing more: a copy, under a name nothing else writes.
 * Declining to bring a game up to date is "not now", and without the copy it
 * would quietly mean "never": the game is still in the store, still not open,
 * and the next thing the author saves goes over the top of it. The same rule
 * the recovery banner keeps, for the same reason, and it costs a file.
 */
async function keepVersionAsItIs() {
  const held = otherVersion.value;
  otherVersion.value = undefined;
  if (!held?.stored || held.opened.fileRevision === undefined) {
    savedStatus.value = "Nothing was changed";
    return;
  }
  try {
    const kept = await documentModel.rescue(
      held.opened.bytes,
      held.opened.fileRevision
    );
    savedStatus.value =
      `Nothing was changed. Your saved game is still here, and a copy of it is `
      + `kept as '${kept}' so nothing you do next can write over it.`;
  } catch (error) {
    savedStatus.value =
      "Nothing was changed, and your saved game could not be copied anywhere "
      + `safe: ${error instanceof Error ? error.message : String(error)}. Do `
      + "not save over it until you have brought it up to date.";
  }
}

/**
 * The author agreed. The steps run against the game as written and the result
 * becomes the open game, unsaved, so the file on disk still says what it
 * always said until the author decides to keep this.
 */
function upgradeOpenedProject() {
  const held = otherVersion.value;
  if (!held || held.opened.age.kind !== "behind") return;
  const upgraded = bringProjectUpToDate(held.opened.written);
  if (!upgraded.ok) {
    savedStatus.value = upgraded.sentence;
    otherVersion.value = undefined;
    return;
  }
  otherVersion.value = undefined;
  // The stored draft's revision is carried across, so saving the brought-up
  // game replaces the game it came from rather than colliding with it.
  fileRevision.value = held.opened.fileRevision;
  replaceProject(
    upgraded.project,
    `${held.named} was brought up to date. Save it to keep it that way`,
    true
  );
}

function replaceProject(next: SourceProject, status: string, isDirty: boolean) {
  project.value = next;
  workspaceKey.value += 1;
  savedStatus.value = status;
  dirty.value = isDirty;
  diagnostics.value = [];
  consoleNotes.value = targetNotes(next);
  // A remounted workspace opens on the section an author lands on, so the rail
  // beside it has to say the same thing.
  activeSection.value = DEFAULT_WORKSPACE_SECTION;
  opened.value = true;
  view.value = "workspace";
}

/** Unsaved work survives every road out of the workspace unless the author
 *  explicitly lets it go. Fails closed where no dialog exists. */
function confirmDiscard(question: string): boolean {
  if (!dirty.value) return true;
  return typeof window.confirm === "function" ? window.confirm(question) : false;
}

function newProject() {
  if (!confirmDiscard(
    "Start a new project? Your unsaved changes will be lost."
  )) return;
  fileRevision.value = undefined;
  replaceProject(createSourceProject(), "New project ready", true);
}

function loadSampleProject(id: string) {
  if (!confirmDiscard(
    "Load this sample game? Your unsaved changes will be lost."
  )) return;
  try {
    const sample = sampleProjects.find((entry) => entry.id === id);
    fileRevision.value = undefined;
    replaceProject(
      createSampleProject(id),
      `${sample?.title ?? "Sample"} loaded. Save it to keep your changes`,
      true
    );
  } catch (error) {
    savedStatus.value =
      `Could not load sample: ${error instanceof Error ? error.message : String(error)}`;
  }
}

function projectChanged(next: SourceProject) {
  project.value = next;
  dirty.value = true;
  consoleNotes.value = targetNotes(next);
  savedStatus.value = "Changes are only in this browser tab";
}

async function analyzeCurrentProject(): Promise<readonly PresentedDiagnostic[]> {
  const text = new TextDecoder().decode(encodeSourceProject(project.value));
  const analysis = props.analyzeProject
    ? await props.analyzeProject(sourceProjectPath, text)
    : analysisWorker
      ? await analysisWorker.analyze(sourceProjectPath, text)
      : analyzeSourceProject(sourceProjectPath, text);
  return presented(analysis);
}

async function saveProject() {
  if (unreadableDraft.value) {
    savedStatus.value =
      "Saving is paused: the stored draft could not be opened and saving would " +
      "overwrite it. Download the draft, then discard it, to save new work.";
    return false;
  }
  busy.value = true;
  try {
    // Editing surfaces hold drafts of their own; a Save that ignored them
    // would persist less than what the author sees on screen.
    const flushed = workspace.value?.flushDrafts() ?? true;
    const saved = await documentModel.save(project.value, fileRevision.value);
    fileRevision.value = saved.fileRevision;
    dirty.value = !flushed;
    savedStatus.value = flushed
      ? "Saved in this browser"
      : "Saved, except an open draft with problems. Fix it and save again";
    // The save itself never waits on validation, but what was just persisted
    // must not claim to be fine when the editor's own loader would reject it.
    try {
      diagnostics.value = await analyzeCurrentProject();
      if (diagnostics.value.length > 0) {
        savedStatus.value +=
          `; validation found ${diagnostics.value.length} problems`;
      }
    } catch (error) {
      savedStatus.value += `; validation failed: ${
        error instanceof Error ? error.message : String(error)}`;
    }
    return true;
  } catch (error) {
    dirty.value = true;
    if (error instanceof UnwritableProjectError) {
      // Nothing was written, which is the point: what is on screen would not
      // have opened again. The problems go to the page that can jump to the
      // field they are about rather than being crushed into one line here.
      diagnostics.value = error.problems.map((problem) => ({
        severity: "error" as const,
        code: problem.code,
        sourcePath: problem.sourcePath,
        instancePath: problem.instancePath,
        message: problem.message
      }));
      savedStatus.value =
        "Nothing was saved, so your stored game is untouched: this project " +
        `would not open again. ${describeProblem(error.problems[0]!)}. ` +
        `See Diagnostics for ${
          error.problems.length === 1 ? "it" : `all ${error.problems.length}`}.`;
      return false;
    }
    if (error instanceof ProjectStoreError && error.code === "REVISION_CONFLICT") {
      // Another tab wrote the file underneath this one. Leaving the revision
      // this tab compares against where it was would wedge it forever, every
      // retry meeting the same conflict, so taking the stored revision here is
      // what makes the next Save a decision the author can actually make, with
      // Export beside it keeping a copy of this tab's work either way.
      const stored = await store.read(sourceProjectPath).catch(() => undefined);
      fileRevision.value = stored?.revision;
      savedStatus.value =
        "Another tab or window saved this project after this one opened it. " +
        "Nothing here is lost. Export to keep a copy of what is on screen, " +
        "or press Save again to write it over theirs.";
      return false;
    }
    savedStatus.value = `Save failed: ${error instanceof Error ? error.message : String(error)}`;
    return false;
  } finally {
    busy.value = false;
  }
}

function presented(analysis: SourceAnalysis): readonly PresentedDiagnostic[] {
  return [
    ...analysis.diagnostics,
    ...analysis.indexDiagnostics.map((diagnostic) => ({
      severity: "error" as const,
      code: diagnostic.code,
      sourcePath: diagnostic.sourcePath,
      instancePath: diagnostic.semanticPath,
      message: diagnostic.message
    }))
  ];
}

/**
 * What checking the game found, said where the author is looking.
 *
 * The answer has a line of its own, in the two colours the answer has, and it
 * is a live region: it is the result of an act, and the act was a press.
 * Carried only on the header line beside "Saved locally", a clean run is four
 * small grey words in the same place, the same size and the same colour as a
 * sentence an author has already learned to stop reading, and a button that
 * appears to give nothing back is indistinguishable from one that does
 * nothing.
 *
 * The header line still carries it too, because that line is the log of what
 * last happened and validation is one of the things that happens.
 */
const validation = ref<{ readonly ok: boolean; readonly message: string }>();

async function validateProject() {
  busy.value = true;
  validation.value = undefined;
  try {
    diagnostics.value = await analyzeCurrentProject();
    const count = diagnostics.value.length;
    savedStatus.value = count === 0
      ? "Validation passed"
      : `Validation found ${count} problems`;
    validation.value = count === 0
      ? { ok: true, message: "Validation passed. Nothing is wrong with this game." }
      : {
        ok: false,
        message: `Validation found ${count} ${
          count === 1 ? "problem" : "problems"}. They are listed under ` +
          "Diagnostics, and each one opens the record it is about."
      };
  } catch (error) {
    const said =
      `Validation failed: ${error instanceof Error ? error.message : String(error)}`;
    savedStatus.value = said;
    validation.value = { ok: false, message: said };
  } finally {
    busy.value = false;
  }
}

// The Nintendo 64 ROM, built by the machine rather than by this page.
//
// The editor does not assemble a ROM and does not patch one. It hands the
// project to a local service that runs the pinned container build, the same
// one the gate runs, so what an author downloads is the checked build.
//
// Everything below exists because that build takes minutes, not seconds. An
// author is owed four things a spinner cannot give them: whether it can be
// done at all before they press anything, roughly how long it will be, where
// the build has got to while they wait, and the toolchain's own words if it
// fails.
//
// The third of those is a running clock rather than a phase name, because the
// thing an author cannot tell from the outside is slow from stuck. A number
// that goes up is proof of life; a sentence that has not changed for ninety
// seconds is not. The service reports no start time, so the clock is the
// editor's own: it counts from the press, which is what the author is
// actually waiting on anyway.
const rom = props.romService ?? new RomService();
const romHealth = ref<RomServiceHealth | null>(null);
const romStatus = ref<RomBuildStatus | null>(null);
const romBuilding = ref(false);
const romMessage = ref("");
const romDetail = ref("");
const romElapsed = ref(0);
let romClock: ReturnType<typeof setInterval> | undefined;

// The clock rewrites the line it is part of, so the message keeps ticking
// between the service's own two-second polls. `aria-live` is deliberately not
// re-announced by this: the status paragraph is polite, and a screen reader
// that read a new second aloud every second would be unusable. What a
// listener gets is the phase changes, and the clock is for the eye.
function startRomClock() {
  stopRomClock();
  romClock = setInterval(() => {
    romElapsed.value += 1;
    const status = romStatus.value;
    if (status) romMessage.value = romProgressText(status, romElapsed.value);
  }, 1000);
}

function stopRomClock() {
  if (romClock !== undefined) clearInterval(romClock);
  romClock = undefined;
}

onBeforeUnmount(stopRomClock);

async function checkRomService() {
  romHealth.value = await rom.health();
}

// What the button says about itself when it cannot be pressed. Named rather
// than inlined because the browser suite asserts on it: the gate runs with no
// service behind the proxy, which is the `container_runtime_missing` path, and
// the editor has to say so in words rather than spin.
const romUnavailableReason = () =>
  romHealth.value === null
    ? "Checking whether this machine can build a ROM…"
    : romHealth.value.ready
      ? ""
      : romHealth.value.message ?? "ROM builds are not available here.";

/**
 * The waiting time, as measured rather than as hoped.
 *
 * `tools/rom_service/README.md` records 1 m 55 s and 1 m 59 s for a cold
 * container build of every target, and under a minute for one campaign ROM
 * into a warm tree. "About a minute" was the warm number offered to somebody
 * who by definition has the cold one, which is the way round that makes an
 * author think the build has hung.
 */
const romDurationSentence =
  "The first one takes about two minutes; later ones are quicker.";

/** Seconds since the press, as m:ss. */
function romElapsedText(seconds: number): string {
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}`;
}

function romProgressText(status: RomBuildStatus, seconds: number): string {
  const clock = ` ${romElapsedText(seconds)} so far.`;
  if (status.state === "queued") {
    return (status.position > 0
      ? `Waiting behind ${status.position} other build${
        status.position === 1 ? "" : "s"}.`
      : "Waiting for the build to start.") + clock;
  }
  if (status.state === "building") {
    return `Building the Nintendo 64 ROM. ${romDurationSentence}${clock}`;
  }
  return "";
}

async function downloadRom() {
  if (romBuilding.value) return;
  // Saving first is a courtesy, never a toll gate. A refused save is exactly
  // the moment an author most needs a way to get their work off this machine,
  // and the service is handed the project on screen rather than the stored
  // copy, so a failure here costs the convenience and nothing else.
  await saveProject();
  romBuilding.value = true;
  romStatus.value = null;
  romMessage.value = "";
  romDetail.value = "";
  romElapsed.value = 0;
  startRomClock();
  try {
    const projectJson = new TextDecoder().decode(
      encodeSourceProject(project.value)
    );
    const { rom: bytes, status } = await rom.build(projectJson, (progress) => {
      romStatus.value = progress;
      romMessage.value = romProgressText(progress, romElapsed.value);
    });
    (props.downloadArchive ?? browserDownload)(
      bytes,
      `${project.value.gameId.replace(/[^a-z0-9._-]+/gi, "-")}.z64`,
      "application/octet-stream"
    );
    romMessage.value =
      `Nintendo 64 ROM ready: ${bytes.length.toLocaleString()} bytes.`;
    romDetail.value = `Built from campaign "${status.campaign}".`;
  } catch (error) {
    if (error instanceof RomServiceError) {
      // The service's own words, and the toolchain's underneath them. A
      // message composed here would be throwing away the only part of a
      // compiler diagnostic an author can act on.
      romMessage.value = error.refusal.message;
      romDetail.value = error.refusal.detail ?? "";
    } else {
      romMessage.value =
        `The ROM build failed: ${
          error instanceof Error ? error.message : String(error)}`;
      romDetail.value = "";
    }
  } finally {
    stopRomClock();
    romBuilding.value = false;
    romStatus.value = null;
  }
}

async function exportProject() {
  // The way out that must never be closed. Export tries to save first, because
  // an author pressing it usually means "keep this", but it hands over what is
  // on screen whether or not the save went through. A refused save and a file
  // another tab is holding are precisely the two moments when an archive is the
  // only road left, and gating the archive on the save would block both.
  const saved = await saveProject();
  // Why the save was refused, in its own words, so that appending the export's
  // outcome does not throw the only explanation away.
  const refusal = saved ? "" : savedStatus.value;
  try {
    const archive = documentModel.exportSnapshot(
      await store.snapshot(),
      project.value
    );
    (props.downloadArchive ?? browserDownload)(
      archive,
      `${project.value.gameId.replace(/[^a-z0-9._-]+/gi, "-")}.grandleon.zip`
    );
    savedStatus.value = saved
      ? "Saved and exported portable project archive"
      : `Exported a portable archive of what is on screen. ${refusal}`;
  } catch (error) {
    savedStatus.value =
      `Export failed: ${error instanceof Error ? error.message : String(error)}`;
  }
}

function openProject() {
  if (!confirmDiscard(
    "Open a project file? Your unsaved changes here will be lost."
  )) return;
  importInput.value?.click();
}

/**
 * The rail asks; the workspace decides. `selectSection` may refuse, because an
 * open editing surface whose draft has problems is not abandoned, and it reports
 * the section that is actually active either way, which is what the rail marks.
 */
function showSection(id: string) {
  workspace.value?.selectSection(id);
}

/** Back to the way in. It opens nothing and discards nothing, so it asks
 *  nothing; every action on that screen keeps its own confirmation. */
function goToStartScreen() {
  view.value = "start";
}

async function importProject(event: Event) {
  const input = event.currentTarget as HTMLInputElement;
  const file = input.files?.[0];
  input.value = "";
  if (!file) return;
  busy.value = true;
  try {
    // Import only reads the archive; the stored draft is untouched until the
    // author saves, so opening a file just to look destroys nothing.
    const loaded = await documentModel.import(
      new Uint8Array(await file.arrayBuffer())
    );
    // A game made with another Grandleon is not opened and nothing is set
    // aside: the author is asked first, and until they answer everything is
    // as it was, the stored draft, this file and whatever was on screen.
    if ("otherVersion" in loaded) {
      askAboutVersion(loaded, file.name, false);
      return;
    }
    // A project opened from a file is the resolution of a paused draft: it is
    // the road an author takes after downloading the draft and repairing it, so
    // holding the pause afterwards would leave them unable to save the very
    // thing they came back with. The old bytes are set aside rather than
    // dropped, exactly as the banner's own button does it.
    const paused = unreadableDraft.value;
    const kept = paused
      ? await documentModel.rescue(paused.bytes, paused.fileRevision)
      : undefined;
    if (paused) {
      fileRevision.value = paused.fileRevision;
      unreadableDraft.value = undefined;
    } else {
      fileRevision.value = loaded.fileRevision;
    }
    replaceProject(
      loaded.project,
      `Opened ${file.name}. Save it to keep it${
        kept ? `. The draft that would not open was set aside as '${kept}'` : ""}`,
      true
    );
  } catch (error) {
    savedStatus.value =
      `Open failed: ${error instanceof Error ? error.message : String(error)}`;
  } finally {
    busy.value = false;
  }
}

function downloadUnreadableDraft() {
  const stored = unreadableDraft.value;
  if (!stored) return;
  (props.downloadArchive ?? browserDownload)(
    stored.bytes,
    "grandleon-recovered-draft.json",
    "application/json"
  );
  savedStatus.value =
    "Downloaded the stored draft. Repair it in a text editor and open it " +
    "again with Open project. This editor takes that file back.";
}

/**
 * Sets the paused draft aside so the author can work again.
 *
 * "Start fresh" means the project now open takes the stored draft's place, and
 * on the road that reaches this banner the project now open is usually a blank
 * one, so a press here is a press away from replacing a finished game with an
 * empty one. The bytes therefore move somewhere nothing overwrites before the
 * pause is released: the author's work stops being one file deep, and the copy
 * rides along in every archive Export makes.
 */
async function discardUnreadableDraft() {
  const stored = unreadableDraft.value;
  if (!stored) return;
  const confirmed = typeof window.confirm === "function"
    ? window.confirm(
      `Set the stored draft aside? A copy is kept in this project, and "${
        project.value.title}", the game open now, is what Save will write ` +
      "from here on."
    )
    : false;
  if (!confirmed) return;
  let kept: string | undefined;
  try {
    kept = await documentModel.rescue(stored.bytes, stored.fileRevision);
  } catch (error) {
    savedStatus.value =
      "The stored draft could not be copied anywhere safe, so it is kept as " +
      `it is and saving stays paused: ${
        error instanceof Error ? error.message : String(error)}. Download the ` +
      "draft to keep it.";
    return;
  }
  fileRevision.value = stored.fileRevision;
  unreadableDraft.value = undefined;
  dirty.value = true;
  savedStatus.value =
    `The draft was set aside as '${kept}' and travels with every export. ` +
    "Save will write the project that is open now.";
}

/**
 * Opens Play on what the author is looking at.
 *
 * Editing surfaces hold drafts of their own, and Play is handed the committed
 * project, so without this an author who raises a weapon's power and presses
 * Play plays the stored number, with nothing on screen saying which of the two
 * they are watching. Save, Export, the ROM button and every section change
 * commit for the same reason.
 *
 * A draft with problems in it cannot be committed, and that refusal is reported
 * where every other refusal is. Play still opens: the surface says what is
 * wrong, and a Stage of the last good numbers is a better answer than a button
 * that does nothing.
 */
function startPlaying() {
  const flushed = workspace.value?.flushDrafts() ?? true;
  if (!flushed) {
    savedStatus.value =
      "Playing the last saved version of an open draft: it has problems, so " +
      "it could not be committed. Fix them and press Play again.";
  }
  playing.value = true;
}

function warnBeforeUnload(event: BeforeUnloadEvent) {
  if (dirty.value) event.preventDefault();
}

onMounted(async () => {
  window.addEventListener("beforeunload", warnBeforeUnload);
  if (typeof Worker !== "undefined" && !props.analyzeProject) {
    // A worker that will not construct costs a slower Validate and nothing
    // else. `analyzeCurrentProject` already falls back to running the same
    // function on this thread when there is no worker, so the only thing a
    // throw here can take away is everything below it — the ROM check, the
    // engine, and the author's own stored draft. That is what it used to do:
    // the browser refuses a worker whose script is not same-origin, and this
    // was the first statement of the hook.
    try {
      analysisWorker = createAnalysisWorkerClient();
    } catch (reason: unknown) {
      analysisWorker = undefined;
      console.warn(
        "The analysis worker could not start; checking on the main thread " +
          "instead.",
        reason
      );
    }
  }
  // Whether this machine can build a ROM at all, asked before the control is
  // offered rather than after it is pressed. A deliberately unawaited promise:
  // it reaches a local service or it does not, and either answer is a label on
  // a button rather than something the editor should wait to open for.
  void checkRomService();
  // The simulation is asynchronous to load but synchronous to use. Start it
  // alongside the draft so Play is usable as soon as the editor is.
  initEncounterEngine().then(
    () => {
      engineReady.value = true;
    },
    (reason: unknown) => {
      savedStatus.value =
        `Could not load the game engine: ${reason instanceof Error ? reason.message : String(reason)}`;
    }
  );
  try {
    const loaded = await documentModel.load();
    if (!loaded) {
      // Nothing stored and nothing begun. The blank project behind the start
      // screen is not work, so it is not unsaved work: an author who has done
      // nothing is not asked to confirm throwing nothing away.
      savedStatus.value = "Nothing saved in this browser yet";
      dirty.value = false;
    } else if ("unreadable" in loaded) {
      unreadableDraft.value = loaded;
      savedStatus.value =
        "Your saved draft could not be opened. It is kept exactly as stored.";
      // The recovery banner and its two buttons are the only useful thing on
      // screen, and they sit above the workspace rather than above a menu.
      opened.value = true;
      view.value = "workspace";
    } else if ("otherVersion" in loaded) {
      askAboutVersion(loaded, "Your saved game", true);
    } else {
      fileRevision.value = loaded.fileRevision;
      replaceProject(loaded.project, "Recovered local browser draft", false);
    }
  } catch (error) {
    savedStatus.value =
      `Could not open local draft: ${error instanceof Error ? error.message : String(error)}`;
    dirty.value = true;
  }
});

onBeforeUnmount(() => {
  window.removeEventListener("beforeunload", warnBeforeUnload);
  analysisWorker?.close();
});
</script>

<template>
  <!-- While Play covers the screen the editor beneath it is inert: not
       focusable, not clickable, invisible to assistive technology. This is
       the other half of PlayMode's dialog contract. -->
  <a class="skip-link" href="#workspace"
    :inert="playing || undefined">Skip to workspace</a>
  <header class="app-header" :inert="playing || undefined">
    <div class="app-identity">
      <img class="app-logo" :src="logoUrl" alt="" width="271" height="271">
      <div>
        <h1>Grandleon Editor</h1>
      </div>
    </div>
    <div>
      <p class="project-status" aria-label="Project status">
        {{ dirty ? "Unsaved changes" : "Saved locally" }}. {{ savedStatus }}
      </p>
      <p v-if="validation" class="validation-result"
        :class="validation.ok ? 'validation-clean' : 'validation-problems'"
        data-testid="validation-result" role="status" aria-live="polite">
        {{ validation.message }}
      </p>
      <!-- Only the verbs that act on the game that is open. The commands that
           replace or discard it live on the start screen, where choosing one
           is a decision rather than a slip of the hand next to Play. -->
      <div v-if="view === 'workspace'" class="project-commands" role="group"
        aria-label="Project commands">
        <button type="button" class="play-command" :disabled="busy"
          @click="startPlaying">
          ▶ Play
        </button>
        <button type="button" :disabled="busy || !dirty" @click="saveProject">
          Save
        </button>
        <button type="button" :disabled="busy" @click="validateProject">
          Validate
        </button>
        <button type="button" :disabled="busy" @click="exportProject">
          Export
        </button>
        <!--
          The cartridge, beside the source archive. Disabled with a reason on
          it rather than hidden: an author who cannot build a ROM here is
          better served by knowing why than by the control quietly not
          existing.
        -->
        <button type="button" data-testid="download-n64-rom"
          :disabled="busy || romBuilding || !(romHealth?.ready ?? false)"
          :title="romUnavailableReason() || 'Build a Nintendo 64 ROM of this project'"
          @click="downloadRom">
          {{ romBuilding ? "Building ROM…" : "Nintendo 64 ROM" }}
        </button>
        <button type="button" class="secondary" :disabled="busy"
          @click="goToStartScreen">
          Start screen
        </button>
      </div>
      <!-- Both shapes the editor hands out: the portable archive from Export,
           and the bare project.json the recovery banner downloads. What the
           file actually is decides how it is read; the list here only decides
           what the file chooser shows first. -->
      <input ref="importInput" class="visually-hidden" type="file"
        accept=".zip,.grandleon.zip,application/zip,.json,application/json"
        @change="importProject">
      <!--
        What the ROM build is doing, or why it cannot. `aria-live` because the
        interesting part of a build this long is that it keeps changing, and a
        screen reader should hear it change without being asked.
      -->
      <p v-if="romMessage || romUnavailableReason()" class="rom-status"
        data-testid="rom-status" aria-live="polite">
        <span>{{ romMessage || romUnavailableReason() }}</span>
        <span v-if="romDetail" class="rom-detail">{{ romDetail }}</span>
      </p>
    </div>
  </header>

  <div v-if="unreadableDraft" class="draft-recovery" role="alert"
    :inert="playing || undefined">
    <p>
      <strong>Your saved project could not be opened.</strong>
      {{ unreadableDraft.reason }}. The file is kept exactly as stored, and
      Save is paused so nothing can overwrite it.
    </p>
    <button type="button" @click="downloadUnreadableDraft">
      Download the stored draft
    </button>
    <button type="button" class="danger" @click="discardUnreadableDraft">
      Set it aside and keep going
    </button>
  </div>

  <div class="app-layout" :class="{ 'without-rail': view === 'start' }"
    :inert="playing || undefined">
    <!-- The real navigation. Every entry is a place the workspace goes to,
         never an anchor to a place the author is already standing in;
         `aria-current` follows what the workspace reports, not what was
         clicked, so a departure it refused is never misreported. -->
    <nav v-show="view === 'workspace'" aria-label="Project"
      class="project-nav project-sections">
      <h2>Project</h2>
      <ul>
        <li v-for="section in sections" :key="section.id">
          <button type="button"
            :aria-current="activeSection === section.id ? 'page' : undefined"
            @click="showSection(section.id)">
            {{ section.label }}
            <span v-if="section.kind === 'diagnostics' && diagnostics.length > 0"
              class="problem-count">
              {{ diagnostics.length }}
              <span class="visually-hidden">
                {{ diagnostics.length === 1 ? "problem" : "problems" }} found
              </span>
            </span>
          </button>
        </li>
      </ul>
    </nav>

    <main id="workspace" tabindex="-1">
      <!--
        A game made with another Grandleon, and the one question worth asking
        about it.

        It replaces the page rather than floating over it, which is how every
        asking surface in this editor works and here costs nothing at all: the
        game is not open, so there is nothing behind this to be modal over, no
        focus to trap and no background to make inert. Cancel and the close of
        the tab mean the same thing, because nothing has happened yet.

        What is on it is the whole decision: which Grandleon made the game,
        which one it needs, and every change bringing it up would make, one
        sentence each in the order they would happen. An author agreeing to a
        list they cannot read is not agreeing to anything.
      -->
      <section v-if="otherVersion" class="version-question"
        data-testid="version-question" aria-labelledby="version-question-title">
        <!-- A file that names no version has no version to name in a heading,
             and "made with Grandleon an unknown version" is what filling the
             gap with a phrase gets you. It says the other true thing instead. -->
        <h2 id="version-question-title">
          <template v-if="otherVersion.opened.age.madeWith">
            {{ otherVersion.named }} was made with
            Grandleon {{ otherVersion.opened.age.madeWith }}.
          </template>
          <template v-else>
            {{ otherVersion.named }} cannot be opened.
          </template>
        </h2>
        <template v-if="otherVersion.opened.age.kind === 'behind'">
          <p class="version-question-need">
            It must be brought up to {{ otherVersion.opened.age.needs }} to open.
            Your file is not changed until you save.
          </p>
          <h3>What changed</h3>
          <ul class="version-changes">
            <li v-for="(change, index) in otherVersion.opened.age.changed"
              :key="index">
              {{ change }}
            </li>
          </ul>
        </template>
        <p v-else class="version-question-refusal" role="alert">
          {{ otherVersion.opened.age.sentence }}
        </p>
        <div class="version-question-commands" role="group"
          aria-label="This game's version">
          <button type="button" class="secondary" @click="keepVersionAsItIs">
            Cancel
          </button>
          <button v-if="otherVersion.opened.age.kind === 'behind'" type="button"
            class="version-upgrade" @click="upgradeOpenedProject">
            Bring it up to date
          </button>
        </div>
      </section>
      <StartScreen v-else-if="view === 'start'"
        :busy="busy"
        :has-open-project="opened"
        :open-project-title="project.title"
        @new-project="newProject"
        @load-sample="loadSampleProject"
        @open-project="openProject"
        @keep-editing="view = 'workspace'" />
      <!-- Hidden, never unmounted: the workspace owns the editing session, so
           a trip to the start screen must not cost the author their drafts or
           their undo history. `display: none` is what keeps it out of the tab
           order and out of the accessibility tree while it waits, and the
           wrapper is what it is hung on, the boundary's root being a fragment
           that a directive cannot hide. -->
      <div v-show="view === 'workspace'">
        <EditorErrorBoundary>
          <ContentWorkspace ref="workspace" :key="workspaceKey"
            :initial-project="project"
            :diagnostics="diagnostics"
            :target-notes="consoleNotes"
            @dirty="dirty = true"
            @change="projectChanged"
            @section="activeSection = $event" />
        </EditorErrorBoundary>
      </div>
    </main>
  </div>

  <PlayMode
    v-if="playing"
    :project="project"
    :ready="engineReady"
    @exit="playing = false"
  />
</template>

<style scoped>
.app-identity {
  display: flex;
  gap: 0.75rem;
  align-items: center;
}
.app-logo {
  width: auto;
  height: 2.75rem;
  image-rendering: pixelated;
}
/* Play is the point of the product, so it leads the command row and does not
   look like the developer verbs beside it. */
.project-commands .play-command {
  background: #2e9e5b;
  color: #ffffff;
  font-weight: 700;
}
/* The rail. Buttons rather than tabs on purpose: `aria-current="page"` on a
   nav of buttons is this editor's established way of saying which of several
   places you are on, and it is already what the collection navigation, both
   catalogue shelves and the record list use. A second interaction model for
   six entries would cost more than it explained. */
.project-nav button {
  display: flex;
  gap: 0.4rem;
  align-items: center;
  justify-content: space-between;
  width: 100%;
  font-weight: 700;
  text-align: left;
}
.project-nav button[aria-current="page"] {
  outline: 3px solid #f2c14e;
  outline-offset: 1px;
}
/* The count is a badge, and the word beside it is the badge's meaning: a
   number alone would leave a screen reader saying "Diagnostics 3". */
.problem-count {
  padding: 0 0.4rem;
  border-radius: 0.6rem;
  background: #a02c2c;
  color: #ffffff;
  font-size: 0.8rem;
}
.draft-recovery {
  display: flex;
  flex-wrap: wrap;
  gap: 0.75rem;
  align-items: center;
  margin: 0.75rem 1rem;
  padding: 0.75rem 1rem;
  border: 1px solid #a02c2c;
  border-radius: 0.5rem;
  background: #fdf3f2;
}
.draft-recovery p {
  flex: 1 1 24rem;
  margin: 0;
}
/* The question about a game's version. A card in the middle of the page the
   author has to answer, rather than a banner above something they could get
   on with: there is nothing to get on with, because the game is not open. */
.version-question {
  max-width: 38rem;
  margin: 2rem auto;
  padding: 1.25rem 1.5rem;
  border: 1px solid #c9cdd4;
  border-radius: 0.6rem;
  background: #ffffff;
}
.version-question h2 {
  margin: 0;
  font-size: 1.1rem;
}
.version-question h3 {
  margin: 1.25rem 0 0.35rem;
  font-size: 0.9rem;
  text-transform: uppercase;
  letter-spacing: 0.04em;
  color: #4a5160;
}
.version-question-need {
  margin: 0.5rem 0 0;
}
.version-changes {
  margin: 0;
  padding-left: 1.25rem;
}
.version-changes li {
  margin: 0.2rem 0;
}
.version-question-refusal {
  margin: 0.75rem 0 0;
}
.version-question-commands {
  display: flex;
  flex-wrap: wrap;
  gap: 0.75rem;
  margin-top: 1.5rem;
}
/* The answer to a press, drawn as an answer: full size, on its own line, and
   in the colour of the answer. Colour is never the only carrier: the sentence
   says which of the two it is in words. */
.validation-result {
  margin: 0.4rem 0 0;
  padding: 0.4rem 0.6rem;
  border-radius: 0.3rem;
  border: 1px solid;
  font-weight: 600;
}
.validation-clean {
  color: #14532d;
  background: #e8f6ec;
  border-color: #2e9e5b;
}
.validation-problems {
  color: #6b1f1f;
  background: #fbeaea;
  border-color: #b23b3b;
}

.rom-status {
  flex-basis: 100%;
  margin: 0.35rem 0 0;
  font-size: 0.85rem;
  line-height: 1.4;
}
/* The detail is the toolchain's or the compiler's own output, which can be
   several lines of diagnostics. It keeps its newlines and is allowed to
   scroll rather than pushing the workspace down the page. */
.rom-detail {
  display: block;
  max-height: 8rem;
  overflow: auto;
  margin-top: 0.25rem;
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  font-size: 0.78rem;
  white-space: pre-wrap;
  opacity: 0.85;
}
</style>
