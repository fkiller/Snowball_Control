# Snowball interaction simulator — revision 5

Revision 5 separates committed value from editing target. Filled keys mean a value is set (including Model/Effort/Access). Exactly one open Project/Session/Model/Effort/Access editor receives an amber inset border, an `Editing` caption and `aria-current`; the top display names the same editor. Access keeps this marker through its confirmation step. Committing a choice clears it; switching editors moves it; entering a modal hides it and returning restores it. Browsing focus within choices remains separate. Machine/Harness cycle immediately and do not leave an editor open. Browser verified Project → Model marker movement and marker removal after committing Fast. Standalone export is synchronized.

This directory is the portable source of truth for the review simulator. Antigravity, Codex, other agents, and humans can read and run it without the original conversation or Codex UI.

## Run

- Open `index.html` directly in a modern browser. It includes all markup, styles, script, and sample data; no installation, CDN, host API, or network access is required.
- Alternatively run `node docs/simulator/preview.cjs` from the repository root and visit `http://127.0.0.1:8768`.
- Edit `controls.html` (canonical fragment), then run `node docs/simulator/preview.cjs --export` to regenerate `index.html`. The optional host Tweak helper is guarded and not required.

## Authoritative behavior (supersedes revisions 1–3)

1. With available registered resources, startup selects Machine → Harness → Project → Session automatically. Use the remembered valid child for each parent, otherwise its first available child in stable order. All four resolved context keys are highlighted. New is an action, not a selected context. Highlight means selected, not proof of authentication; real transport must report connection separately.
2. Machine and Harness cycle directly on their own keys. The sample has five machines and two harnesses. On an actual context switch, resolve descendants within the new parent scope immediately; do not route through an artificial empty screen. Old sessions continue independently. Preserve drafts by their original destination; the prototype retains them in memory but has no draft-restoration UI.
3. Project and Session open a list on the top display and five matching choices on key row 3. Left rotation moves the browsing focus and advances the five-item window; both surfaces update together. Rotation does not commit a choice. Press the knob or a choice key to commit; the conversation opens immediately. No Close key occupies row 3. The filled context keys retain the committed values while browsing. A Focused caption identifies navigation focus independently of selected highlighting.
4. Workspace and Settings remain fullscreen modals. Their upper-left anchor returns to the exact previous surface. Workspace shows the directory path on the top display and anchor. Its lower 15 keys are file/folder entries, initially all unselected. Left rotation scrolls by one five-item ROW, clamped at either end, without a cursor moving horizontally or selecting an item. Press a file/folder key to open it. Knob push is unused in the modal. File reading uses the top display; left rotation then scrolls file lines. Up returns to its directory; only the explicitly opened file may highlight while being read.
5. Questions keep three visible option rows and a fixed Other/Submit/Speak/Later/Stop bottom row. Left rotation scrolls options; selection and submission remain distinct. Right rotation is always volume, push mute. Speak is unavailable outside session/question content. Modal browsing is the deliberate exception to the session voice bottom row.
6. Real missing resources are not normal navigation steps: no registered machine requires enrollment, unavailable harness requires setup/reconnect, zero projects requires host-side project creation, and zero sessions resolves to a new conversation. Never fabricate a real resource or imply an unpaired machine is authenticated. These exceptional provisioning flows are not implemented in this fixture.

## State and limitations

State is in the `s` object inside the fragment script. `memory` stores last harness per machine, project per machine/harness, and session per machine/harness/project. It lasts for this simulation instance only; refresh resets it. Production must persist stable resource IDs and validate remembered IDs against discovery results. The sample labels are not resource IDs or an API contract.

Sample directory names and file contents are illustrative, not repository reads. Audio, pairing, settings, harness events, session content, and Stop are simulated. Follow UI is a preference toggle, not desktop focus integration. Incoming events are disabled while modals are open; production needs a destination-bound pending-request queue. No firmware changed.

## Reproduction checks

| Action | Expected |
|---|---|
| Load page | Four context keys highlighted; conversation visible |
| Project, rotate down five times | Firmware–Experiments visible both above and on row 3; no Close |
| Choose Firmware; cycle Harness twice | Codex/Firmware restored with a selected session |
| Session, rotate through twelve entries, choose one | Row 3 tracks the top list; chosen session opens |
| Workspace, rotate down once | Items 6–20 shown; no item selected |
| Rotate up, open docs, Up | Parent path and viewport restored; no implicit selection |
| Open a file, rotate down, Up, Workspace | File text scrolls, then directory, then prior surface restored |
| Simulated event: User question; rotate down four times | Options E–G; bottom actions remain fixed |

Browser verified startup, five-item project pagination, scoped project restoration, and vertical workspace scrolling with no selected item. Revision 3 browser checks additionally covered file reading/return and scrolled question submission. Script syntax and Git whitespace checks pass. Physical device, provider connections, durable persistence, and provisioning paths have not been tested here.
