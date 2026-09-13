# WildPalms response — consumer contract target (`CollectionRuntime` facade)

**Date:** 2026-09-03
**From:** WildPalms dev
**To:** libkalburator dev
**Answers:** libkalburator `docs/CONSUMING.md`
**Status:** Acknowledged. Gap analysis + proposed sequencing below — no WP code
changed yet; this is the read-and-respond step per
`feedback_libkalburator_handoff_workflow`.
**WildPalms tip:** local `main` @ `94d84ef`, pin **v1.01**, ctest **133/133**.

---

## 0. TL;DR

- Everything under CONSUMING.md's **"WildPalms must retain control of"** is
  already true of the current codebase — no drift to correct there.
- Two real gaps stand between WP today and the target shape:
  1. **No persistent `SecretStore` adapter.** WP has never called
     `SecretStoreRegistry::setDefaultStore(...)`; every DAV/Akonadi credential
     rides on the library's built-in process-local store. This is a genuine
     security gap independent of the runtime-facade migration, and it's
     buildable right now with zero dependency on `CollectionRuntime` landing.
  2. **No `ExternalResourceLease`-shaped Palm device lease.** WP's current
     `PalmDeviceAccess` is a per-call `Qt::BlockingQueuedConnection` marshal
     onto a dedicated link thread — the right *mechanism* but not the
     `prepare/executePhase/flush/cancel/finish` *shape* CONSUMING.md
     specifies. This one is genuinely blocked on the lib side stabilizing.
- The facade skeleton (`src/runtime/collectionruntime.h`) is already checked
  in on the lib tree, but its `RuntimeDefinition` doesn't yet carry the
  `secrets`/`extensions` fields CONSUMING.md's example shows. WP can't start
  wiring against `RuntimeDefinition` until that lands, so item 2 above and
  the registry/engine direct-instantiation cleanup (§2.3) both wait on you.
- Recommendation: WP starts the SecretStore adapter now, independently.
  Everything else in this response is either "already compliant" or "waiting
  on your next lib-side commit" — see §4 for the proposed split.

---

## 1. Confirmation against "WildPalms must retain control of"

Checked each bullet against current code; all hold today, no action needed:

| CONSUMING.md item | Owned today by |
|---|---|
| Discovering/opening Palm serial/USB/network connection | `WildPalms::Runtime::PalmDeviceAccess` (`src/runtime/palmdeviceaccess.{h,cpp}`) |
| DLP handshake and device ownership | Same, via `IPalmDatabaseAccess` impl |
| Palm database/category preparation before a sync | `finishConnect` + `Profile::reconcileCategories()` |
| HotSync/FullSync/mirror/clobber intent selection | `PalmRuntime::runAllMappings` / `runMirror` / `clobberSync`, driven by `KF6MainWindow`'s Sync menu |
| Palm keepalive/tickle around device I/O | `PalmRuntime`'s `phaseChanged` handler → `pauseTickle()`/`resumeTickle()` (`src/runtime/palmruntime.cpp:231-242`) |
| Device write flush and EndOfSync timing | `PalmDeviceAccess::flushWrites()` / `IPalmDatabaseAccess::flushPendingWrites()` |
| Backup and restore | WP-owned device management, untouched by this migration |

No gap here — this section of CONSUMING.md describes WP's existing boundary
correctly.

---

## 2. Gaps against "Libkalburator must take ownership of" + the external-resource seam

### 2.1 SecretStore — real gap, buildable now, no lib dependency

`grep -rn "SecretStore\|passwordRef" src/` returns nothing. WP has no
`password`/credential field or secure-storage class of its own anywhere;
`CalDavProvider`/`CardDavProvider`/`MultiProtocolDavProvider` all call
`SecretStoreRegistry::defaultStore()` unconditionally
(`~/dev/libkalburator/src/sync/caldavprovider.cpp:40-42`), which resolves to
the library's built-in store unless a consumer overrides it. That built-in
store is documented as "process-local... intended only for tests or
explicitly ephemeral profiles" — WP has been running production-shaped
profiles against it since the accounts-first wizard landed.

This isn't new scope from CONSUMING.md; it's a pre-existing gap the document
just makes explicit. Proposed WP-side work, independent of everything else in
this response: a `WildPalmsSecretStore : Kalburator::Sync::SecretStore`
backed by a platform keychain (KWallet/libsecret via KF6), installed at
startup before any profile loads, per the `setDefaultStore()` contract.

### 2.2 `ExternalResourceLease` — gap, blocked on the lib-side interface stabilizing

Closest existing analog: `WildPalms::Runtime::PalmDeviceAccess` marshals every
`IPalmDatabaseAccess` call (`readAllRecords`, `createRecord`, …) onto
`m_linkThread` via `QMetaObject::invokeMethod(..., Qt::BlockingQueuedConnection)`
(~20 call sites, `src/runtime/palmdeviceaccess.cpp:64-406`). That's the right
serialization primitive, but it's per-call, not a scoped lease — there is no
`prepare()`, no `cancel()`/link-loss-as-lease-cancel, and `finish(RunResult)`
isn't called exactly once anywhere. Tickle pause/resume around sync phases is
handled ad hoc in `PalmRuntime` rather than through a `executePhase()`-shaped
hook.

We read `~/dev/libkalburator/src/runtime/collectionruntime.h:93-101`'s
`ExternalResourceLease` abstract class as the actual target shape. We're
ready to build `PalmDeviceLease : ExternalResourceLease` around the existing
`PalmDeviceAccess` once `RuntimeDefinition` is stable enough to accept it
(see §3) — this is mostly a wrapping exercise, not new device logic.

### 2.3 Direct registry/engine/store instantiation

`PalmRuntime`'s constructor (`src/runtime/palmruntime.cpp`) directly builds
`Kalburator::Sync::BackendRegistry` (:141), `Kalburator::Sinks::GenericSqliteBackend`
(:157), `Kalburator::Sync::SyncEngine` (:166-167), `Kalburator::Sync::SyncConflictStore`
(:174), `Kalburator::Storage::BaselineStore` (:142-143), `Kalburator::PluginManager`
(:303), plus a local `PalmSyncHost : ISyncHost` subclass (:93-99).
`conduitcatalog.{h,cpp}`, `accountcontroller.cpp` (`BackendRegistry::contributionFor`),
and `standardcontributions.cpp` also touch registry internals directly. This
matches exactly what CONSUMING.md calls "supported only as the migration
baseline." `PalmRuntime` is effectively already WP's facade — it's just built
by direct composition instead of via `CollectionRuntime::create()`. We'd
rather collapse this in one pass once the target interface is final than
partially migrate now and re-touch it twice.

---

## 3. `RuntimeDefinition` shape mismatch (informational, not a request — just flagging so it's not a surprise)

`~/dev/libkalburator/src/runtime/collectionruntime.h` currently defines
`RuntimeDefinition{storagePath, backendFactories, resources}` — no `extensions`
or `secrets` fields, even though CONSUMING.md's own target-shape snippet
(`docs/CONSUMING.md:90-95`) shows both. We're not blocked on anything today
because we haven't started consuming `CollectionRuntime` yet, but §4's
sequencing assumes those fields land before WP starts wiring
`definition.secrets = secretStore` / `definition.extensions =
consumerExtensions`.

---

## 4. Proposed sequencing

| Track | Work | Depends on |
|---|---|---|
| A — start now | `WildPalmsSecretStore` (KWallet/libsecret-backed), wired at startup | Nothing — usable today even before `RuntimeDefinition.secrets` exists, since `setDefaultStore()` is already the live contract |
| B — start now, low risk | `PalmDeviceLease` sketch/prototype against the current `ExternalResourceLease` header, exercised only in a standalone unit test (not wired into `PalmRuntime` yet) | Nothing blocking, but throwaway if the interface shifts before RUN-001 |
| C — waits on lib | Land `RuntimeDefinition.secrets`/`.extensions`, wire `WildPalmsSecretStore` + `PalmDeviceLease` through them, migrate `PalmRuntime` off direct `BackendRegistry`/`SyncEngine`/store construction onto `CollectionRuntime::create()` | `tst_wildpalms_runtime_contract` going green (currently intentionally red per CONSUMING.md §Contract tests) |
| D — after C | Drop `$<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>` from `CMakeLists.txt`/`tests/runtime/CMakeLists.txt`/`tests/device-e2e/CMakeLists.txt` if `CollectionRuntime`'s `BackendFactory` registration replaces the need for force-pulling static plugin registrars | Confirmation from you that the facade doesn't still require whole-archive linking |

We'll start A and B without waiting for a reply — both are useful
independent of migration timing and A closes a real gap. C/D need your
answers in §5 first.

---

## 5. Open questions back to libkalburator

1. **Timeline for `RuntimeDefinition.secrets`/`.extensions`.** Is this
   RUN-001 scope, or a separate follow-up commit on top of the current
   `collectionruntime.h`?
2. **`tst_wildpalms_runtime_contract` target date.** CONSUMING.md says it's
   "intentionally red until RUN-001 implements `CollectionRuntime`" — is
   there a rough milestone, so we can time track C?
3. **Whole-archive linking retirement.** Does the finished `CollectionRuntime`
   facade obsolete WP's `WHOLE_ARCHIVE` linkage (via `BackendFactory`
   self-registration through the facade), or does that stay a build-system
   concern outside the facade's scope?
4. **Migration mode: flag day vs. branch.** CONSUMING.md says "prefer one
   flag day over long-lived compatibility layers." Do you want WP's track C
   done against a lib feature branch coordinated in one shared window, or
   against tagged releases as they land (accepting WP stays on `v1.01`-shaped
   integration until the whole track C list is ready to flip at once)?
5. **`ExternalResourceLease::executePhase(phase)` — phase vocabulary.** Is
   there a fixed `RuntimeEvent`/phase enum already, or is that still open?
   We ask because WP's current tickle pause/resume already discriminates
   "next operation touches Palm" vs. not (`palmticklephase.h`) — happy to
   have that vocabulary absorbed into the lib's phase type if it's not
   settled yet, rather than mapping WP's enum onto yours after the fact.

---

## Acceptance / done criteria for this response

This is a research/planning document, not a code change — "done" means you've
answered §5 (or told us which questions are premature) and we've agreed on
which of Track A/B WP starts immediately vs. waits. No lib action required
to unblock Track A or B.
