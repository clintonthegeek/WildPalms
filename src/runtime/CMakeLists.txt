# WildPalmsRuntime — managers for the new WP plugin ABI (Phase E.8) plus
# Full-Sync host-interface implementations (relocated from src/fullsync/
# in E.15b). Houses BackendPluginManager + PluginActionManager + shared
# metadata helpers + InstallSourceCollector + the SyncEngine-facing
# WP host implementations consumed by libkalburator.

find_package(KF6 REQUIRED COMPONENTS CoreAddons Config I18n)
find_package(Qt6 REQUIRED COMPONENTS Concurrent)

add_library(WildPalmsRuntime STATIC
    # Substrate A1 — PimPlugin promoted to the conduit descriptor. Its
    # out-of-line virtuals (matchesCollection / categorySlotNames) are the
    # PimPlugin key function, so its vtable/typeinfo are emitted here and
    # pulled into WildPalmsCore.so alongside palmruntime.o (PalmDeviceAccessLib)
    # and routemapping.o, which both enumerate descriptors.
    ../plugins/pimplugin.h
    ../plugins/pimplugin.cpp
    # Substrate A1 — stock-conduit factory: the single source of truth for
    # "which conduits exist". Consumed by PalmRuntime::registerPalmPlugins()
    # and by the wizard (transient descriptor-query set). Constructs the four
    # concrete plugins; their ctors resolve at the WildPalmsCore.so link.
    conduitcatalog.h
    conduitcatalog.cpp
    pluginmetadatahelpers.h
    pluginmetadatahelpers.cpp
    # K.8b T13: BackendPluginManager deleted (IBackendPluginV2 also gone;
    # plugin discovery is now in-process via Kalburator::Plugin / PalmRuntime
    # since K.8b T6). InstallSourceCollector::drainPluginBlobs that took a
    # BackendPluginManager* is also commented out at the call site below.
    pluginactionmanager.h
    pluginactionmanager.cpp
    simpleactioncontext.h
    simpleactioncontext.cpp
    # Phase E.15a — folder + cross-plugin blob aggregator for the
    # install action.
    installsourcecollector.h
    installsourcecollector.cpp
    # Phase E.15b — Full-Sync host-interface implementations relocated
    # from src/fullsync/. WildPalmsFullSync no longer exists.
    # K.8b T8: CalendarCollection_WP deleted (was ICalendarCollection shim
    # for the eager-preload loop; no longer needed after K.7 + T0 removed
    # the CalendarPluginWriter null-guard requirement).
    conflictpresenter_wp.h
    conflictpresenter_wp.cpp
    conflictresolver_wp.h
    conflictresolver_wp.cpp
    syncconfigstore_wp.h
    syncconfigstore_wp.cpp
    synchost_wp.h
    synchost_wp.cpp
    # F.1a — App-level profile registry.
    profileregistry.h
    profileregistry.cpp
    # T7 — QMessageBox-backed IMassDeleteGuard with worker-thread marshalling.
    massdeleteguardpresenter.h
    massdeleteguardpresenter.cpp
    # Phase E.16 — pilot-link factory header (forward-decls only;
    # impl compiled into WildPalmsCore where the device-side
    # libraries can be linked without an AUTOMOC cycle).
    pilotlinkconnectionfactory.h
    # hub↔remote routing Task 2 — pure translator: persisted SyncMapping → RouteSpec
    routemapping.h
    routemapping.cpp
    # Substrate A3 — pure category reconciliation over AppInfo bytes (matches
    # desired names case-insensitively, claims free slots, preserves the
    # app-specific tail). Uses CategoryInfo (WildPalmsCore) — resolved at the
    # WildPalmsCore.so link.
    categoryreconciler.h
    categoryreconciler.cpp
    # Clobber-sync Task 4 — modal dialog for per-conduit Palm-clobber
    # selection. Pure Qt::Widgets; no engine/device knowledge.
    clobberdialog.h
    clobberdialog.cpp
)

target_sources(WildPalmsRuntime PRIVATE
    # M2 Task 2/3 — PalmDeviceAccess: IPalmDatabaseAccess wrapper that
    # marshals all calls to a dedicated link thread via BlockingQueuedConnection.
    # NOTE: compiled into PalmDeviceAccessLib (below) while syncrunner_wp.cpp
    # is broken by Plan 1 (Task 12 re-merges these into WildPalmsRuntime once
    # the syncrunner is rewritten).
)

target_include_directories(WildPalmsRuntime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)

target_link_libraries(WildPalmsRuntime
    PUBLIC
        Qt::Core
        Qt::Widgets       # QIcon, QWidget in core headers (ibackendplugin.h)
        KF6::CoreAddons
        KF6::ConfigCore   # F.1a — KSharedConfig in profileregistry.cpp
        KF6::I18n         # T7 — KLocalizedString in MassDeleteGuardPresenter
        # Phase E.15b — fullsync host impls implement Kalburator::Sync
        # interfaces with virtual methods; vtable emission requires an
        # actual link, not just an include path.
        Kalburator::Sync
    # WildPalmsCore is exposed only on the INTERFACE link line — consumers
    # that pull in WildPalmsRuntime get the shared lib — but WildPalmsRuntime
    # itself does NOT link WildPalmsCore. This breaks the Phase-E.9 cycle
    # where KF6MainWindow (inside WildPalmsCore) needs BackendPluginManager.
    # WildPalmsCore is the SHARED consumer; linking Runtime PRIVATE there
    # embeds these static objects, and the Core-side symbols they need
    # (ipluginaction.cpp / ibackendplugin.cpp) are already compiled into
    # the same .so.
    INTERFACE
        WildPalmsCore
)

set_target_properties(WildPalmsRuntime PROPERTIES POSITION_INDEPENDENT_CODE ON)

# M2 Task 2/3 — Isolated build unit for PalmDeviceAccess so the test target
# compiles while syncrunner_wp.cpp is still broken (Plan 1 breakage; Task 12
# merges this back into WildPalmsRuntime once the syncrunner is rewritten).
# M2 Task 5 — PalmRuntime skeleton + PalmRunResult type compiled here for the
# same reason (WildPalmsRuntime is still broken by syncrunner_wp.cpp).
add_library(PalmDeviceAccessLib STATIC
    palmdeviceaccess.h
    palmdeviceaccess.cpp
    palmrunresult.h
    palmruntime.h
    palmruntime.cpp
    palmticklephase.h   # P2: phase-scoped tickle pause helper (header-only)
    accountcontroller.h
    accountcontroller.cpp
    palmtickle.h
    palmtickle.cpp
    # F.1c.1 T2 — shared contribution-registration helper used by
    # PalmRuntime ctor and (T11) KF6MainWindow ctor.
    standardcontributions.h
    standardcontributions.cpp
    # Substrate A2 — credential-less local-folder provider + its contribution,
    # registered by registerStandardContributions above. QObject (Q_OBJECT) so
    # it needs AUTOMOC, which this target already has.
    localfolderprovider.h
    localfolderprovider.cpp
    localfolderconfigwidget.h
    localfolderconfigwidget.cpp
    localfoldercontribution.h
)
target_include_directories(PalmDeviceAccessLib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
target_link_libraries(PalmDeviceAccessLib
    PUBLIC
        Qt::Core
        Qt::Concurrent  # M3 — QtConcurrent::run used by backup/restore
        # M2 Task 5 — PalmRuntime's unique_ptr members (BackendRegistry,
        # SyncEngine) require complete types at destruction; add Kalburator::Sync
        # so the destructor compiles. Task 12 merges this lib back into
        # WildPalmsRuntime which already links Kalburator::Sync.
        Kalburator::Sync
        # Phase Ic Task 3 — AccountController persists provider sidecar via
        # KConfig/KConfigGroup.
        KF6::ConfigCore
)
set_target_properties(PalmDeviceAccessLib PROPERTIES POSITION_INDEPENDENT_CODE ON)
