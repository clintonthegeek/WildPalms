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
