// SPDX-License-Identifier: MIT
import { ref } from "vue";

/**
 * Where a control's keystrokes live between being typed and being committed.
 *
 * A control bound straight at the stored record and committed only when the
 * browser fires `change` loses work two ways, and both were measured in a
 * browser rather than reasoned about.
 *
 * One: between a keystroke and leaving the field the words exist only in the
 * DOM. Nothing announces them, so the header says the project is saved, the
 * close-the-tab guard stays quiet, and a Save persists less than the author can
 * see. A field is not a place work is safe.
 *
 * Two: such a control cannot survive a redraw. Anything that writes the
 * record, a sibling editor, an undo or another surface, draws the control again
 * with the stored value, and Vue re-applies it over what is being typed. No `input`
 * announces the replacement, and because the field then holds what it held when
 * it was focused, no `change` fires on the way out either.
 *
 * So the keystroke goes here: held, announced as unsaved, and drawn by the
 * control that owns it, so a redraw writes back what is already on screen.
 * `change` and a flush are what turn it into an edit of the project.
 *
 * **The text is held raw, exactly as typed.** That is what makes this right for
 * a number as well as for a sentence: "-" and "" and "007" are all things a
 * half-typed number passes through, and none of them should be parsed, rounded
 * or written into the project on the way past. A number that reached the record
 * per keystroke would also be a value the diagnostics and the forecast react to
 * while nobody has finished saying it.
 *
 * **One control at a time**, which is all a keyboard can produce: a second
 * control being typed into commits the first, exactly as leaving it would.
 */
export interface KeystrokeDraft {
  /** What a control draws: its own pending keystrokes, or the stored words. */
  shown(key: string, stored: string): string;
  /** A keystroke. Held and announced, and not in the project yet. */
  type(key: string, text: string, commit: (text: string) => void): void;
  /** Leaving a control, which is one undoable edit. */
  leave(key: string, text: string, commit: (text: string) => void): void;
  /**
   * Commits whatever is pending; called when a field is left, before anything
   * that rearranges what the controls are drawing, and by a Save. Always true:
   * a held keystroke is committed the same way leaving the field commits it,
   * and whether the result is a record the project accepts is answered where
   * that is already answered.
   */
  flush(): boolean;
}

/**
 * @param announce Tells the surrounding editor there is work in progress,
 *   ultimately the project header and the close-the-tab guard.
 */
export function useKeystrokeDraft(announce: () => void): KeystrokeDraft {
  const held = ref<{ key: string; text: string } | undefined>();
  let commitHeld: ((text: string) => void) | undefined;

  function flush(): boolean {
    const pending = held.value;
    const commit = commitHeld;
    held.value = undefined;
    commitHeld = undefined;
    if (pending && commit) commit(pending.text);
    return true;
  }

  function type(key: string, text: string, commit: (text: string) => void) {
    if (held.value && held.value.key !== key) flush();
    held.value = { key, text };
    commitHeld = commit;
    announce();
  }

  return {
    shown: (key, stored) => held.value?.key === key ? held.value.text : stored,
    type,
    /**
     * The control's value is read here as well as on the way in, rather than
     * trusting that an `input` was seen first. A commit that works only when
     * the event it expects came before it is the same fragility one layer down.
     */
    leave(key, text, commit) {
      type(key, text, commit);
      flush();
    },
    flush
  };
}
