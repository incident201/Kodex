# Verification of Kodex with a real Codex

Two sets of checks are recorded here, and they used different Codex versions:

| Check | Date | Codex CLI and daemon |
| --- | --- | --- |
| Full interactive matrix (the tables below) | 23 September 2026 | 0.156.1 |
| Re-check, see [Re-check on Codex 0.162.1](#re-check-on-codex-01621) | 10 October 2026 | **0.162.1** |

The full matrix was **not** repeated on 0.162.1. Environment of the matrix run: Qt 6.11.2,
KDE Frameworks 6.30.0, Breeze, C++23. Environment of the re-check: Qt 6.12.0, KDE
Frameworks 6.30.0, GCC 16.2.1, CMake 4.4.4.

## Re-check on Codex 0.162.1

| Check | Result |
| --- | --- |
| Protocol reference regenerated with `codex app-server generate-json-schema --experimental` | `protocol/codex-0.162.1.json` |
| Requests of the core scenarios validated against that schema (`conversation`, new task without rollout, archived history, sessions without a project, attachments, pasted attachments, project switching, Git operations, pairing) | Passed |
| Handshake with the installed daemon: `initialize`, `model/list`, `project/list` (`liveHandshake`) | Passed |
| One short real turn in an ephemeral thread with a read-only sandbox (`liveTurn`) | Passed |
| `thread/list` with `projectId: null`, used for sessions without a project, against the installed Codex (standalone `app-server --stdio`) | Returned the sessions of the account |
| Installed build (`cmake --install`) started with the Russian translation found through `XDG_DATA_DIRS` | Passed |
| Desktop entry (`desktop-file-validate`) and AppStream metadata (`appstreamcli validate --pedantic --no-net`) | Passed |
| Real window of the built application on a Wayland session, light and dark Breeze, with a long conversation, pictures and the Git panel | Looked at; no QML warnings |

Memory with a conversation of 150 large screenshots and 30 pictures embedded in the
history, scrolled 30 times on a GPU-backed window: 422–435 MiB and flat. Before the fixes
(a fixed glibc mmap threshold, pictures stored as files instead of base64 in the message
rows, thumbnails decoded at the size shown, and pictures released when a message leaves
the view) it reached about 2 GiB.

The checks below ran against the installed `codex`, the existing account sign-in and
the real app-server. The interactive checks load the same QML and C++ backend and use
Qt mouse, key and input events; they do not fake model answers. The built `build/kodex`
was also started separately and opened a real session. The regular regression tests with
the fake server complement these checks.

## Results of the matrix run on 0.156.1

| Scenario | Result |
| --- | --- |
| Creating a project and a new task | Passed |
| Streaming answer and a file changed by the model | Passed; file content verified |
| Codex diff and the Git working tree diff | Passed |
| Staging and committing a file created by the agent | Passed in a temporary repository |
| Reconnecting the client and restoring the history | Passed |
| Plan mode → two structured questions | Passed |
| Answering by choosing an option and by free text | Passed through the interface |
| Finished plan → "Keep planning" | Passed; no files are created |
| Finished plan → "Implement plan" | Passed; the mode changes and code is created |
| Generated Python unittest | Passed |
| Stopping a running `sleep 30` command | Passed; the server returned `interrupted` |
| Continuing a stopped task | Passed; a new answer arrived |
| `gpt-6-astra`: `low` and `medium` | Passed; real answers and server settings checked |
| `gpt-6-sol`: `low` and `medium` | Passed; real answers and server settings checked |
| Fast on/off | Passed; `serviceTier` checked on the server |
| Switching between two tasks in the sidebar | Passed; histories do not mix |
| Approval: decline, then allow | Passed; the decline created no file, the approval did |
| Read only / Workspace / Approve for me / Full access | Passed; the server accepted the settings |
| `/compact` | Passed; a `contextCompaction` item arrived |
| Archiving, reading the archive, restoring | Passed |
| The real window of the built application | Smoke run without QML or protocol errors |
| Light and dark theme and 850×600 | Passed; separate processes with isolated KDE palettes |

The negative approval scenario only uses decisions offered by the server. If `decline`
is not offered, `cancel` is chosen; the permission is not granted and no file is created.
The positive scenario only checks writing into a temporary directory owned by the test.

## Fixes found by the checks

- A new task uses the answer of `thread/start`: until its first turn it has no rollout,
  so an immediate `thread/resume` was an error.
- The archive is read with `thread/read`; continuing is allowed after unarchive.
- Interrupted commands get the status "Stopped", including in the history. A regression
  test covers the safe handling of the JSON status value.
- Explicitly disconnecting clears the pending RPC callbacks, so late answers do not call
  components that are already closed.
- A finished plan has an explicit choice to implement it; delivering the plan does not
  start any code.
- The answer buttons stay pinned under the scrollable form. Long options wrap, and the
  standard RadioButton no longer overlaps the labels.
- The model, effort and access menus are attached to the compact selectors and open
  upwards.
- The chat keeps the end of the answer when the window is resized or the plan panel
  appears.
- An approval shows the exact command and the working directory at once.
- The diff is highlighted; in a narrow window the Git actions block collapses to leave
  room for the patch. The composer buttons stay inside the panel.

## Independent visual review

A separate agent reviewed the screenshots and the sources without editing files. The
findings were rechecked after the fixes. Final conclusion: all P1/P2 findings are fixed;
in the checked light and dark themes and at 850×600 no further fixes are needed. The
artifacts are saved in `build/live-artifacts/`:

- `01-questions*.png`, `02-questions-answered.png` — real questions;
- `03-plan-ready.png`, `04-implemented.png` — the decision to implement and the result;
- `05-running*.png`, `06-stopped.png` — a running and a stopped task;
- `07-model-menu.png`, `08-compact-window.png` — the menu and the compact window;
- `10-approval-decline.png`, `11-approval-accept.png` — approvals;
- `12-compacted.png`, `13-archived-task.png`, `14-restored-task.png` — the history;
- `15-diff-light.png`, `16-diff-dark.png`, `17-diff-compact*.png` — a real patch with
  added and removed lines in both KDE themes.

The KDE palettes are loaded before `QApplication` from separate temporary `kdeglobals`
files. The test also checks the actual brightness of the image. Changing
`QApplication::setPalette` inside one process is not used for this check.

## Running the checks again

```sh
cmake --build build
ctest --test-dir build --output-on-failure

KODEX_LIVE_WORKFLOW=1 ./build/kodex_live_tests
QT_QPA_PLATFORMTHEME=kde KODEX_LIVE_INTERACTIVE=1 ./build/kodex_interactive_tests
./scripts/test-native-ui.sh
```

Single scenarios can be run on their own: `taskSwitching`, `realInteractiveScenarios`,
`approvalsCompactionAndArchive`, `visualDiffThemes`. The live tests use up account
limits. They create their own temporary projects and tasks and delete them afterwards;
user repositories and the shared daemon are not restarted.

## Not checked by hand

A new browser sign-in and sign-out, physical phone pairing and updating a working Codex
installation were not performed. The existing sign-in was used; the remote control state
was read from the real server. Pairing and the updater have separate automated checks
with a simulated server and installer. An external push was only checked against a local
bare remote, never against GitHub.
