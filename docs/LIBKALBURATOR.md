# libkalburator — consumption story

**Date:** 2026-05-21 (post-merger).
**Upstream repo:** [`codeberg.org/clintonthegeek/libkalburator`](https://codeberg.org/clintonthegeek/libkalburator) — public.
**Currently pinned tag:** `v0.52-phase-p-merge-ready` (consumed via `FetchContent`; see `CMakeLists.txt`).
**License:** LGPL-3.0-only — compatible with WildPalms' GPLv3.
**CMake targets:** `Kalburator::Sync` (the engine + storage + sinks + plugin host), `Kalburator::Types` (shared vocabulary headers).

## What it is

A Qt6 / KF6 sync engine + provider/backend framework, extracted from PlanStan. WildPalms and PlanStan share it. The engine drives two-way sync between any pair of `SyncBackend`s (Palm device, local files, CalDAV, CardDAV, Akonadi, web feeds, Org); the conflict pipeline, baseline storage, ID-mapping store, plugin host, and provider lifecycle all live upstream.

WildPalms does not own a sync engine. See `docs/SYNC_ENGINE_ARCHITECTURE.md` for what the engine does; see `docs/PLUGIN_ABI.md` for how WildPalms's plugins plug into it.

## How WildPalms consumes it

`CMakeLists.txt` uses `FetchContent` to pull libkalburator pinned to the tag above. The first configure of an empty build directory downloads the source under `build-fetchcontent/_deps/libkalburator-src/` and builds it as a subproject. The dev override — pointing CMake at a sibling `~/dev/libkalburator/` worktree — is optional and useful only when actively iterating on the library; it is no longer the required layout.

Default build dir: `build-fetchcontent/`. The previous flat-layout convention (sibling `../libkalburator/` checkout, building into `build-dev/`) is **no longer required**, though it still works. The `.clangd` pointer file is the one place where a stale `build-dev/` reference will bite — keep it in sync with the directory you actually configure into.

Build flags WildPalms toggles:

| Flag | Default | Purpose |
| --- | --- | --- |
| `KALBURATOR_HAVE_ORG_IO` | `OFF` | WildPalms does not ship Org-mode I/O. Leave off. |
| `KALBURATOR_HAVE_AKONADI` | `OFF` | Optional. Turn on if you want Akonadi-backed remote accounts. |

CMake targets WildPalms links against:

- `Kalburator::Sync` — the main library. Always linked.
- `Kalburator::Types` — header-only shared vocabulary (`SyncResult`, `SyncMapping`, `CollectionInfo`, `Shape`, etc.). Inherited transitively from `Kalburator::Sync`.

WildPalms's plugin libraries (`wildpalms_calendar_static`, etc.) link `Kalburator::Sync` normally and inherit its namespaced public include interface through that link. The production graph no longer extracts target properties or force-links the aggregate library; specialized registrar tests may retain explicit force-link fixtures where they test static registration.

## What we use from it

| Subsystem | Header location upstream | Used in WildPalms by |
| --- | --- | --- |
| `Plugin` + `PluginManager` | `src/plugin/plugin.h`, `src/plugin/pluginmanager.h` | `PalmRuntime::registerPalmPlugins()` |
| `SyncEngine` (+ `BlobSyncEngine`) | `src/engine/syncengine.h` | `PalmRuntime::hotSync/fullSync/copyPalmToPC/copyPCToPalm` |
| `BackendRegistry` + `SyncBackend` | `src/sync/backendregistry.h`, `src/sync/syncbackend.h` | `PalmBackend`, every plugin's `*BlobBackend`, `AccountController` |
| `BaselineStore` (SQLite, `blob_baselines_v3`) | `src/storage/baselinestore.h` | `PalmRuntime` — the engine's primary baseline |
| `IDMappingStore` (SQLite, `sync_id_mappings`) | `src/journal/idmappingstore.h` | reused indirectly through the engine; WildPalms also has a legacy JSON `IDMappingStore` in `src/sync/journal/` for `SyncState::pendingConflictCount` |
| `ConflictHandler` API | `src/conflict/conflicthandler.h` | `KalburatorInteractiveConflictHandler` + per-plugin overlays |
| `Sinks::RawFilesBackend` | `src/sinks/rawfilesbackend.h` | default PC-side sink for auto-generated mappings |
| `Sinks::MockBlobBackend` | `src/sinks/mockblobbackend.h` | per-plugin e2e tests |
| Provider framework | `src/plugin/*providerplugin.h` | `AccountController` for CalDAV / CardDAV / Akonadi |
| Stock plugins (domain definitions) | `src/plugin/stock_plugins.{h,cpp}` | `PalmRuntime::registerPalmPlugins()` calls `Kalburator::registerStockPlugins()` |

## What is not (yet) upstream that we'd like

- **Domain definitions for additional Palm-only kinds.** If WildPalms ever wants to sync, say, ExpenseDB or NotePadDB, those domains have to be added to libkalburator's stock plugins (or registered as WildPalms-private domains). Currently only `blob`, `calendar`, `contacts`, `memo`, `todo` are defined.
- **A blob baseline test that exercises `LocalBlobBackend` with a non-Mock target.** Tracked at `~/dev/libkalburator/docs/2026-05-21-localblobbackend-cross-id-mapping.md` — a suspected duplicate-on-second-sync bug raised by WildPalms's E.16 deferral (d). WildPalms-side investigation is paused until PlanStan has a chance to weigh in.
- **A WebCalendar provider.** The WP-side WebCalendar plugin was deleted in 2026-05-21 because of a cross-thread parenting bug; its only upstream contribution was `Kalburator::Sync::IcsFeedFetcher`, which stays in the library and is still useful to PlanStan and to a future WP redesign.

## Coordination protocol

libkalburator is maintained by the same author as WildPalms and PlanStan. The current discipline (post engine-merger):

1. **Bugs found upstream while working in WildPalms:** fix in `~/dev/libkalburator/`, push to Codeberg with a descriptive commit message, then bump the WP pin and rebuild. Do not monkey-patch around the library inside WildPalms — the whole point is shared code.
2. **WP-driven API changes:** land upstream with a commit message that explains the WildPalms motivation; include a libkalburator ctest demonstrating the new contract; update PlanStan's pretest baseline if needed. WildPalms picks up the change on the next pin bump.
3. **Pretest discipline:** every libkalburator change must pass PlanStan's ctest baseline before landing. PlanStan is the regression guard; WildPalms is the second consumer. See `docs/archived/engine-merger-2026/README.md` for the campaign history that established this.
4. **Cross-repo notes:** when a WildPalms-driven concern is not yet a bug-with-a-fix (e.g. the LocalBlobBackend cross-id-mapping suspicion), drop a dated note into `~/dev/libkalburator/docs/` so PlanStan devs can read it. Commit it to libkalburator's `main`. WildPalms-side issues that don't ever leak into the library stay in `docs/superpowers/specs/` here.

## When to look where

| You want to… | Read |
| --- | --- |
| Understand the engine internals | `~/dev/libkalburator/docs/phase0/` — the upstream phase docs, top-down index in `README.md`. |
| See an unfinished WP-driven concern | `~/dev/libkalburator/docs/2026-*.md` — dated notes with WP provenance. |
| Understand the WP-side consumption surface | This file + `docs/SYNC_ENGINE_ARCHITECTURE.md` + `docs/PLUGIN_ABI.md`. |
| Bump the pinned version | Edit `CMakeLists.txt`'s `FetchContent_Declare(... GIT_TAG …)`, reconfigure, run ctest. If WP tests fail and PlanStan's don't, the regression is WP-specific — file it upstream. |
| Run against an unreleased libkalburator | Override `FETCHCONTENT_SOURCE_DIR_LIBKALBURATOR=<path>` on the CMake command line, or use the dev sibling-layout override block in `CMakeLists.txt`. |

## Build prerequisites

Qt 6.5+, KF6 (CoreAddons, I18n, KIO, CalendarCore, Contacts, DAV, XmlGui, WidgetsAddons, Holidays). CMake 3.16+. C++20.

The Akonadi optional subsystem requires `KPim6::AkonadiCore` + Extra-CMake-Modules ECM module path (`ECM` package); on Arch / Manjaro this means installing `extra-cmake-modules` and `kpim6` packages. WildPalms's `CMakeLists.txt` does the dependency dance only when `KALBURATOR_HAVE_AKONADI=ON`.
