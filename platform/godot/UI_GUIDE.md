# PC play interface

The play interface is judged by one rule: gameplay should be fun, quick, and
intuitive. A persistent element must help the player's next decision. Details
that are useful only while configuring, diagnosing, or learning belong in a
contextual surface instead of occupying the battlefield.

## Information budget

| Information | Where it appears | Why it earns space |
|---|---|---|
| Stage | Compact top strip | Keeps the current story beat clear. |
| Round and active side | Compact top strip | Changes whether the player can act and matters to timed objectives. |
| Position, terrain, units, remaining health | Board | These are the tactical state. |
| Cyan / amber / red ownership | Health bars and cursors | Answers who may command a unit without another text label. |
| Selected name, HP, move, range, readiness | Temporary card while inspecting | These numbers change the immediate choice. |
| Current action and legal choices | Beside the acting unit | Keeps eyes on the board and disappears after the choice. |
| Current control hint | Compact bottom strip | Pairs a button badge with one-word actions usable right now. |

Cyan is the first local control, amber the second, and red the enemy. Those
colours appear directly on cursors, health, and legal action tiles instead of
occupying a permanent legend. Controller connection changes use a brief toast;
they are not permanent status rows.

## Information kept off the battlefield

- Graphics profile, sound level, stick sensitivity, and frame timing live in
  Settings.
- Save state and campaign restart live under Save / Load.
- Complete button maps live in the pause and setup surfaces. Play shows only
  the buttons relevant to the current choice.
- The board renders full-bleed behind the interface. Empty framing is never
  reserved for a panel that may not be needed.
- `P1` and `P2` badges, persistent controller-readiness rows, generic panel
  titles, and a permanent “offline story” label duplicate information already
  communicated by colour or context and are deliberately absent.

Emberbound uses authored dialogue and deterministic built-in enemy tactics.

## Interaction rules

1. A fixed formation proceeds directly from story to battle.
2. Moving a character opens legal actions beside that character, including
   doing nothing; each co-op control owns its own popup.
3. An actionable popup chooses from positions beside its character and at the
   board corners. It must avoid covering characters, either cursor, legal
   movement or target tiles, the unit card, and the other player's menu.
4. Unit details and transient notices use the same live-geometry placement
   rule. They move to a clear edge rather than covering the action they
   describe.
5. Both controls remain live during friendly movement and presentation.
6. Refused input says what happened in one short toast and returns control
   immediately.
7. A screen with one safe primary action focuses it first. A screen with no
   meaningful choice is skipped.
