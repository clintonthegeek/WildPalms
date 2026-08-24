# UX Shakedown Remediation — running log

Tracks execution of the roadmap "UX shakedown remediation" items (F-numbers
reference `2026-08-22-first-run-simulated-shakedown.md`). One commit per
cycle; this doc is updated in the same commit.

---

## Cycle 1 — Tier 1: first-run wizard + run-lifecycle integrity (F1, F3/F11)

**Commit:** this one.

### F1 — startup stopgap routed through the real wizard

The empty-registry branch of `resolveStartupProfile()` no longer shows the
bare name-prompt stopgap (`showProfilePickerStopgap`, deleted). It now runs
`runProfileWizard()` (the accounts-first wizard) and, on acceptance,
registers + persists + loads the profile via a new shared helper
`KF6MainWindow::createAndLoadProfileFromWizard()` — the same tail
`onNewProfile` uses (register → `writeWizardResultToProfile` → rollback on
failure → `loadProfile`). A cancelled wizard leaves the app idle, creating
nothing (no more hollow profiles).

Collateral fix (F15 partial): **"Change Sync Folder…"** was the stopgap's
other caller — it invoked the name-prompt and *created a new profile* when
the user picked a folder. It now shows a real directory picker, saves the
choice to the profile, and reloads it.

### F3/F11 — runStarted/runFinished pairing + re-entrancy

PalmRuntime:

- Every early-return that follows an emitted `runStarted` now emits the
  matching `runFinished` + `syncCompleted` (`hotSync`, `fullSync`,
  `runMirror`, `clobberSync`, and `runAllMappings`' no-enabled-mappings
  path). The result reports `success=false` with an explanatory message
  ("No sync targets configured — nothing to sync", …), which surfaces in
  the log dock / status bar via the normal `onPalmRunFinished` path.
- New public `PalmRuntime::isSyncRunning()`. All six public entry points
  pre-guard on it and reject a re-entrant call with a ready failed future
  ("A sync is already running") **before** emitting any signals — the
  in-flight run keeps sole ownership of the current signal pair.
- `runAllMappings`' internal guard remains as a backstop for direct calls;
  it also emits no signals (same ownership rationale).

KF6MainWindow:

- New `reportSyncAlreadyRunning(opLabel)` helper called by all sync handlers
  (`onHotSync`, `onFullSync`, `onCopyPalmToPC`, `onClobberPalmFromPC`,
  `onBackup`, `onRestore`): re-entrant clicks now log
  "*\<op\> ignored — a sync is already running*" to the log dock and flash
  the status bar instead of silently resetting the conduit chips.

### Tests

- New `tests/runtime/tst_palm_runtime_run_lifecycle.cpp` (4 cases): empty-
  mappings pairing for hotSync/fullSync/copyPalmToPC; re-entrant hotSync
  rejected mid-run without a second signal pair; `isSyncRunning` lifecycle.
- `tst_kf6mainwindow_startup.cpp` rewritten off the deleted stopgap seam:
  cancel-leaves-nothing + accept-creates-and-loads-profile (asserts the
  created profile is not hollow), plus the two auto-load regressions now
  asserting the wizard is never reached.
- Full suite: **131/131 pass** (was 130/130).

### Verification notes for user testing

- Fresh `~/.wildpalms` (empty registry) → launch → the real wizard opens.
- Cancel at any wizard page → app stays idle, no profile created.
- Finish → profile loads with accounts/bindings intact (no hollow state).
- HotSync on a profile whose runtime has zero enabled mappings → dashboard
  returns to idle immediately with an explanatory log line (no eternal
  spinner).
- Double-click Sync Now mid-run → second click is a visible no-op.

---

## Cycle 2 — Tier 2: conflict honesty (F10)

**Commit:** this one. "Apply Resolutions (Sync)" is no longer wired to
nothing.

### Engine-side store attached (prerequisite)

`PalmRuntime` now constructs a per-profile engine-side
`Kalburator::Sync::SyncConflictStore` at `.state/sync-conflicts.db` (next to
`hub.db`) and hands it to `SyncEngine::setSyncConflictStore()`. Previously
WP never attached one, so `syncConflictStore()` returned nullptr, deferred
conflicts were never persisted, and `rehydratePendingResolutions()`
early-returned — resolutions could not replay even in principle.
`PalmRuntime::applyConflictResolutions()` maps UI decisions
(`UseSource/UseTarget/UseBoth/Merge/Skip`) onto engine resolutions
(`SourceWins/TargetWins/Duplicate/CustomMerge/Skip`) and writes them into
the store; it lives on PalmRuntime because WildPalmsCore cannot include the
engine-side `synctypes.h` (WP-local file collision). Known caveat (O52,
lib-side): merged payloads are not persisted, so a replayed Merge falls
back to the engine's automatic merger; `DeleteBoth` has no persisted
counterpart yet and is reported as unapplied rather than mis-mapped.

### Window wiring

`onConflictBadgeClicked()` connects `ConflictReviewDialog::
applyResolutionsRequested` → new `KF6MainWindow::applyConflictResolutionsToEngine()`:
reads resolved-but-unapplied records from the UI store, bridges them via
the runtime, marks them applied in the UI store, and logs an honest summary
("*Applied N conflict resolution(s) — they will take effect on the next
sync*"). Unsupported decisions log a warning instead of silently vanishing.

### Tests

- New `tests/runtime/tst_kf6mainwindow_conflict_apply.cpp`: safe no-op
  without a runtime; full round-trip (engine-recorded conflict id → UI
  detection mirror → UseSource → engine store holds `SourceWins`, marked
  applied); DeleteBoth left unapplied.
- `tst_palm_runtime_run_lifecycle` gains `engineConflictStore_
  attachedAndFunctional`.
- Full suite: **132/132 pass**.

### Verification notes for user testing

- Engineer a two-sided edit of one record between syncs with AskUser
  policy → conflict badge appears → review dialog → resolve + "Apply
  Resolutions" → next HotSync applies the chosen side and the conflict
  stops re-presenting.

---

## Cycle 3 — Tier 4: feedback-honesty batch (F13, F14, F15, F16)

**Commit:** this one.

### F13 — cancellation is no longer an error

`PalmRunResult` gains `bool cancelled`. The engine watchers (fixpoint loop,
mirror) and the clobber `.then()` set it when the run was cancelled;
`KF6MainWindow::onPalmRunFinished` reports "*\<op\> cancelled*" via
logWarning + a transient status-bar message instead of "[ERROR] … finished
with errors: Sync cancelled".

### F14 — "Last sync" actually updates

Successful runs stamp `Profile::setLastSyncTime(result.endTime)` +
`save()` in `onPalmRunFinished`, then refresh the dashboard model. Failed
and cancelled runs do not stamp.

### F15 — dead UI wired up

- **Navigate Ctrl+1–5**: were actions in the collection whose signals had
  zero consumers (no menu even referenced them). Now wired: Memos /
  Contacts / Calendar / Tasks switch to their conduit pages; Dashboard
  selects the Patchbay page (the dashboard strip itself is not a page).
- **Review &Conflicts...**: was force-disabled forever ("for a future
  reimplementation"). It now opens the same review dialog as the badge,
  its enablement follows the live pending-conflict count, and the "(%1)"
  label is driven by that count instead of the stale legacy SyncState
  loop (removed).
- **Change Sync Folder...** already fixed in Cycle 1 (real directory
  picker instead of creating a hollow profile).

### F16 — Quit quits; tray-hiding explains itself

- File→Quit used to call `QWidget::close()`, which the hide-to-tray
  `closeEvent` silently swallowed: window vanished, process lived on.
  Quit now routes through new `appQuitRequested()` which sets a bypass
  flag before closing (wired via SLOT string — ActionManager only knows
  `KXmlGuiWindow*`).
- Closing to tray shows a one-time explanation ("Wild Palms keeps
  running… Use File → Quit to exit completely").
- Both paths first guard an in-flight sync with a confirmation dialog
  ("may leave records half-written").

### Tests

- New `tests/runtime/tst_kf6mainwindow_lastsync.cpp`: successful run stamps
  profile.conf (verified through a fresh Profile load); failed/cancelled
  runs leave lastSyncTime untouched. New seams:
  `runPalmFinishedForTest`, `currentProfilePathForTest`.
- Full suite: **133/133 pass**.

### Verification notes for user testing

- Start a HotSync against a slow device and press Cancel → log/status say
  "cancelled", dashboard does NOT claim errors.
- After any successful sync the dashboard's "Last sync" updates and
  survives an app restart.
- Ctrl+2..5 jump between PIM pages; Review Conflicts enables only while
  conflicts are pending.
- File→Quit exits the app even with minimize-to-tray on; clicking window
  X hides to tray once with an explanation; quitting mid-sync asks first.

---

## Cycle 4 — F12: engine diagnostics reach the Log dock

**Commit:** this one.

`PalmRuntime::runLog` (declared since M6b, never emitted, never connected)
is now live:

- **Bridges in the PalmRuntime ctor:** engine `transcodingWarning`
  ("Transcoding warning on \<cal\> (record \<uid\>): data loss: …" — the
  known lossy alarm transcode will finally be visible) and
  `syncPassStarted` ("Sync pass N of M — re-running mappings dirtied by
  the last hop", making the multi-hop fixpoint observable).
- **Per-mapping failures:** the pass watcher emits "Mapping failed: \<err\>"
  as each mapping fails instead of folding everything into one end-of-run
  summary line.
- **Dock wiring:** `loadProfile()` connects `runLog → LogWidget::logInfo`.

Deliberately out of scope: a global `qInstallMessageHandler` bridge for
raw lib qWarning/qDebug output. The signal-level surfaces above cover the
user-relevant diagnostics; a category-filtered Qt message handler is
queued as polish if raw engine chatter is ever wanted in the dock.

### Tests

`tst_palm_runtime_run_lifecycle` gains `mappingFailure_emitsRunLog`: a
seeded record + injected target-side create failure must produce a runLog
line containing "failed" (and a failed run result).

Full suite: **133/133 pass.**

### Verification notes for user testing

- Sync against a device/Palm baseline that triggers the lossy alarm
  transcode → the Log dock shows the transcoding warning (previously only
  console-visible).
- Watch a multi-hop HotSync move data → "Sync pass 2 of 3" lines appear.

---

## Cycle 5 — Tier 5 quick wins (F5, F8, F18)

**Commit:** this one.

- **F5:** the wizard's sync-target rows now distinguish the two silences:
  connected accounts with no matching collections keep the "No matching
  collections on your accounts." hint; accounts that have not connected
  show a new "Accounts have not connected yet — only local files are
  available until they do." hint instead of nothing.
- **F8:** category-reconcile failures at connect time (AppInfo write
  failure, no-free-slot) are surfaced via `logMessage` into the Log dock
  alongside the existing console warnings.
- **F18:** the window title stays one consistent form —
  "Wild Palms - Palm Pilot Synchronization[ — \<profile\>]" — instead of
  being replaced by a bare "Wild Palms - \<profile\>" on load.

### Tests

`tst_targetpickerpage` gains `hintShownWhenAccountsNotYetConnected`
(all four conduit rows show the new hint for an unconnected account).
Full suite: **133/133 pass.**

---

## Cycle 6 — device detection guidance + pre-launch enumeration (F6, F7)

**Commit:** this one.

- **F7:** `PalmDeviceMonitor::enumerateExistingDevices()` — `start()` now
  scans already-present tty devices for Palm hardware (vendor 0830,
  grouped by owning USB device, serial preserved) and emits
  `palmDetected` for each hit. A Palm plugged in before app launch is no
  longer invisible until replugged.
- **F7b:** manual Device → Connect with no profile used to fail
  console-only ("Cannot connect: PalmRuntime not initialized") while the
  action stayed enabled. It now logs AND shows a dialog pointing at
  File → New Profile….
- **F6:** the plug-in-without-HotSync-button timeout headline now carries
  guidance: "No HotSync data detected on any of N port(s). Press the
  HotSync button on your Palm, then try again."

### Tests

No new unit tests (udev enumeration requires kernel state; the e2e
device harness is the right future home — Phase-2 matrix can pin it).
Full suite: **133/133 pass.** Hardware verification: plug a Palm before
launching → detection fires on startup without pressing HotSync.
