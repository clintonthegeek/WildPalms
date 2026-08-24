# Phase E.8 — WildPalmsRuntime tests (BackendPluginManager,
# PluginActionManager, plugin metadata helpers).

function(add_wildpalms_runtime_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            KF6::CoreAddons
            WildPalmsCore
            WildPalmsRuntime
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_wildpalms_runtime_test(tst_pluginmetadatahelpers tst_pluginmetadatahelpers.cpp)
# K.8b T13: tst_backendpluginmanager + tst_plugin_factory_roundtrip deleted
# along with BackendPluginManager + IBackendPluginV2.
add_wildpalms_runtime_test(tst_profileregistry tst_profileregistry.cpp)
add_wildpalms_runtime_test(tst_profile_registry_integration
    tst_profile_registry_integration.cpp)
add_wildpalms_runtime_test(tst_profileregistry_rename tst_profileregistry_rename.cpp)
add_executable(tst_profilemenucontroller tst_profilemenucontroller.cpp)
target_link_libraries(tst_profilemenucontroller
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        KF6::XmlGui
        KF6::ConfigCore
        KF6::I18n
        WildPalmsCore
        WildPalmsRuntime
)
add_test(NAME tst_profilemenucontroller COMMAND tst_profilemenucontroller)
set_tests_properties(tst_profilemenucontroller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
add_wildpalms_runtime_test(tst_simpleactioncontext tst_simpleactioncontext.cpp)
add_wildpalms_runtime_test(tst_pluginactionmanager tst_pluginactionmanager.cpp)
# tst_palm_device_access links PalmDeviceAccessLib directly (kept after
# the M5c deletion of SyncRunner_wp because PalmDeviceAccessLib has the
# same include paths and Qt::Core linkage that this test needs).
add_executable(tst_palm_device_access tst_palm_device_access.cpp)
target_link_libraries(tst_palm_device_access
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime  # Phase J Task 7: palmruntime.cpp uses CalendarCollection_WP
        # palmruntime.cpp instantiates PilotLinkPalmDatabaseAccess; pull in
        # WildPalmsPalmDevice and its transitive pisock deps to resolve it.
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_device_access COMMAND tst_palm_device_access)
set_tests_properties(tst_palm_device_access PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
# K.8b T13: tst_plugin_factory_roundtrip deleted, so the dependency on
# dummy_backend/dummy_action is gone too (dummy_backend itself deleted).
add_executable(tst_palm_runtime_hotsync tst_palm_runtime_hotsync.cpp)
target_link_libraries(tst_palm_runtime_hotsync
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime  # Phase J Task 7: palmruntime.cpp uses CalendarCollection_WP
        # Phase Ia.5: WHOLE_ARCHIVE pulls in libkalburator's static
        # DomainPlugin registrars so dispatchSync's
        # DomainRegistry::findByDomain() finds a plugin at sync time.
        # Without WHOLE_ARCHIVE the linker drops the registrars as
        # unreferenced TUs and dispatchSync errors with "no edge path".
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        # palmruntime.cpp directly instantiates PilotLinkPalmDatabaseAccess
        # (WildPalmsPalmDevice) on connectDevice(); link in the device lib and
        # its transitive pisock deps so the symbol resolves at link time.
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_hotsync COMMAND tst_palm_runtime_hotsync)
set_tests_properties(tst_palm_runtime_hotsync PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Change-detection unit tests (grows across the multi-hop-sync tasks): mock
# revision counter (Task 2), PalmBackend forwarding (Task 3), PalmChangeDetection
# mixin (Task 5), and the shouldContinueSync loop helper (Task 6). The later cases
# pull in WildPalmsRuntime + Kalburator::Sync (SyncResult), so the full link set
# below (mirroring tst_palm_runtime_hotsync) is needed by the end, not dead weight.
add_executable(tst_palm_change_detection tst_palm_change_detection.cpp)
target_link_libraries(tst_palm_change_detection
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_change_detection COMMAND tst_palm_change_detection)
set_tests_properties(tst_palm_change_detection PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# K.8b T16: PalmRuntime::cancelSync() — mid-sync cancel restoration
add_executable(tst_palm_runtime_cancel_sync tst_palm_runtime_cancel_sync.cpp)
target_link_libraries(tst_palm_runtime_cancel_sync
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_cancel_sync COMMAND tst_palm_runtime_cancel_sync)
set_tests_properties(tst_palm_runtime_cancel_sync PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M5a Task 2: KalburatorInteractiveConflictHandler unit test
add_executable(tst_kalburator_interactive_conflict_handler
    tst_kalburator_interactive_conflict_handler.cpp
)
target_link_libraries(tst_kalburator_interactive_conflict_handler
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Concurrent
        WildPalmsAppConflict
        Kalburator::Sync
        # WildPalmsCore provides ConflictDialog (referenced by conflictdialogbridge.cpp).
        # The bridge's exec() is never called in tests (the hook short-circuits first),
        # but the linker still requires the symbol to be resolved.
        WildPalmsCore
)
add_test(NAME tst_kalburator_interactive_conflict_handler
    COMMAND tst_kalburator_interactive_conflict_handler
)
set_tests_properties(tst_kalburator_interactive_conflict_handler PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M5a Tasks 7-8: PalmRuntime::setConflictHandler
add_executable(tst_palm_runtime_conflict_handler
    tst_palm_runtime_conflict_handler.cpp)
target_link_libraries(tst_palm_runtime_conflict_handler
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime  # Phase J Task 7: palmruntime.cpp uses CalendarCollection_WP
        Kalburator::Sync
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_conflict_handler
    COMMAND tst_palm_runtime_conflict_handler)
set_tests_properties(tst_palm_runtime_conflict_handler PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M5b Task 1: PalmRuntime::isRunning()
add_executable(tst_palm_runtime_is_running tst_palm_runtime_is_running.cpp)
target_link_libraries(tst_palm_runtime_is_running
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime  # Phase J Task 7: palmruntime.cpp uses CalendarCollection_WP
        Kalburator::Sync
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_is_running
    COMMAND tst_palm_runtime_is_running)
set_tests_properties(tst_palm_runtime_is_running PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M5b Task 2: PalmRuntime::reloadMappings(QJsonArray)
add_executable(tst_palm_runtime_reload_mappings
    tst_palm_runtime_reload_mappings.cpp)
target_link_libraries(tst_palm_runtime_reload_mappings
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime  # Phase J Task 7: palmruntime.cpp uses CalendarCollection_WP
        Kalburator::Sync
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_reload_mappings
    COMMAND tst_palm_runtime_reload_mappings)
set_tests_properties(tst_palm_runtime_reload_mappings PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Clobber-sync Task 3: PalmRuntime mapping classification helpers.
# Pure in-memory unit test on palmDirectMappingsForDomain / isPalmDirectMapping
# — mirrors tst_palm_runtime_reload_mappings' link set.
add_executable(tst_palm_mapping_classification
    tst_palm_mapping_classification.cpp)
target_link_libraries(tst_palm_mapping_classification
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime
        Kalburator::Sync
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_mapping_classification
    COMMAND tst_palm_mapping_classification)
set_tests_properties(tst_palm_mapping_classification PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M5b Task 3: defaults guard — connectDevice skips defaults if mappings present
add_executable(tst_palm_runtime_default_mappings_only_when_empty
    tst_palm_runtime_default_mappings_only_when_empty.cpp)
target_link_libraries(tst_palm_runtime_default_mappings_only_when_empty
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime  # Phase J Task 7: palmruntime.cpp uses CalendarCollection_WP
        Kalburator::Sync
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_default_mappings_only_when_empty
    COMMAND tst_palm_runtime_default_mappings_only_when_empty)
set_tests_properties(tst_palm_runtime_default_mappings_only_when_empty PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Phase Ic Task 3: AccountController smoke + integration tests.
add_executable(tst_account_controller tst_account_controller.cpp)
target_link_libraries(tst_account_controller
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        PalmDeviceAccessLib
        WildPalmsRuntime  # Phase J Task 7: palmruntime.cpp uses CalendarCollection_WP
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_account_controller COMMAND tst_account_controller)
set_tests_properties(tst_account_controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# L.9: per-provider enable/disable + fan-out to mappings
add_executable(tst_provider_enable_fanout tst_provider_enable_fanout.cpp)
target_link_libraries(tst_provider_enable_fanout
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        PalmDeviceAccessLib
        WildPalmsRuntime
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_provider_enable_fanout COMMAND tst_provider_enable_fanout)
set_tests_properties(tst_provider_enable_fanout PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# L.10: per-mapping enable toggle persistence
add_executable(tst_mapping_enable_persists tst_mapping_enable_persists.cpp)
target_link_libraries(tst_mapping_enable_persists
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        PalmDeviceAccessLib
        WildPalmsRuntime
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_mapping_enable_persists COMMAND tst_mapping_enable_persists)
set_tests_properties(tst_mapping_enable_persists PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Phase Ic Task 11: AccountsPage widget tests.
add_executable(tst_accounts_page tst_accounts_page.cpp)
target_link_libraries(tst_accounts_page
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        PalmDeviceAccessLib
        WildPalmsRuntime  # Phase J Task 7: palmruntime.cpp uses CalendarCollection_WP
        WildPalmsPalmDevice
        WildPalmsCore
        WildPalmsAppAccounts
        pisock
        bluetooth
        usb
)
add_test(NAME tst_accounts_page COMMAND tst_accounts_page)
set_tests_properties(tst_accounts_page PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.1c.1 T11 — KF6MainWindow integration test (NewProfileWizard via seam)
add_executable(tst_kf6mainwindow_newprofile tst_kf6mainwindow_newprofile.cpp)
target_link_libraries(tst_kf6mainwindow_newprofile
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsAppWizard
        WildPalmsAppAccounts
        WildPalmsCore
        WildPalmsPalmDevice
        WildPalmsRuntime
        PalmDeviceAccessLib
        KF6::I18n
        KF6::ConfigCore
        KF6::XmlGui
        pisock
        bluetooth
        usb
)
add_test(NAME tst_kf6mainwindow_newprofile COMMAND tst_kf6mainwindow_newprofile)
set_tests_properties(tst_kf6mainwindow_newprofile PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.1c.1 T10 — NewProfileWizard e2e test
add_executable(tst_newprofilewizard tst_newprofilewizard.cpp)
target_link_libraries(tst_newprofilewizard
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsAppWizard
        WildPalmsAppAccounts
        WildPalmsCore
        WildPalmsPalmDevice
        KF6::I18n
        KF6::ConfigCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_newprofilewizard COMMAND tst_newprofilewizard)
set_tests_properties(tst_newprofilewizard PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.1c.1 T9 — ReviewPage unit test
add_executable(tst_reviewpage tst_reviewpage.cpp)
target_link_libraries(tst_reviewpage
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsAppWizard
        WildPalmsAppAccounts
        WildPalmsCore
        WildPalmsPalmDevice
        KF6::I18n
        KF6::ConfigCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_reviewpage COMMAND tst_reviewpage)
set_tests_properties(tst_reviewpage PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.1c.1 T6 — TargetPickerPage unit test
add_executable(tst_targetpickerpage tst_targetpickerpage.cpp)
target_link_libraries(tst_targetpickerpage
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsAppWizard
        WildPalmsAppAccounts
        WildPalmsCore
        WildPalmsPalmDevice
        KF6::I18n
        KF6::ConfigCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_targetpickerpage COMMAND tst_targetpickerpage)
set_tests_properties(tst_targetpickerpage PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.1c.1 T5 — NamePage unit test
add_executable(tst_namepage tst_namepage.cpp)
target_link_libraries(tst_namepage
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsAppWizard
        WildPalmsAppAccounts
        WildPalmsCore
        WildPalmsPalmDevice
        KF6::I18n
        KF6::ConfigCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_namepage COMMAND tst_namepage)
set_tests_properties(tst_namepage PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Accounts-first wizard — domain filter unit test (tst_domainfilter) retired in
# substrate A1: collectionMatchesDomain folded into PimPlugin::matchesCollection,
# its cases absorbed by tst_conduit_descriptor.

# F.1c.0 — AccountFormWidget unit test
add_executable(tst_accountformwidget tst_accountformwidget.cpp)
target_link_libraries(tst_accountformwidget
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsAppAccounts
        # Same dependency chain as tst_accounts_page — WildPalmsAppAccounts
        # transitively pulls in PalmDeviceAccessLib which references KPilotLink
        # (in WildPalmsCore) plus pisock / bluetooth / usb. Link them all here.
        WildPalmsCore
        WildPalmsPalmDevice
        pisock
        bluetooth
        usb
)
add_test(NAME tst_accountformwidget COMMAND tst_accountformwidget)
set_tests_properties(tst_accountformwidget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.3 T12: tst_mapping_row_dialog + tst_mapping_editor_dialog removed —
# MappingRowDialog / MappingEditorDialog retired in favour of the
# SyncMappingsPage graph view.

# M5c Task 2: smoke test — KF6MainWindow per-plugin KPageWidget wiring.
# Loads real WildPalms plugins via BackendPluginManager (same path
# KF6MainWindow uses) and asserts every plugin claiming hasMainView()=true
# returns a non-null QWidget from createMainView() — the contract that
# onBackendPluginLoaded depends on.
# K.8b T6: rewritten — plugins are now STATIC in WildPalmsCore; no longer
# discovered via KPluginFactory.  CMAKE_BINARY_DIR define and KF6::CoreAddons
# dep removed; plugin headers live under ${WP_SRC}/plugins/.
add_executable(tst_main_window_plugin_pages_populated
    tst_main_window_plugin_pages_populated.cpp)
target_link_libraries(tst_main_window_plugin_pages_populated
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        WildPalmsCore
        WildPalmsRuntime
        wildpalms_calendar_static
        wildpalms_contacts_static
        wildpalms_memo_static
        wildpalms_todos_static
)
add_test(NAME tst_main_window_plugin_pages_populated
    COMMAND tst_main_window_plugin_pages_populated)
set_tests_properties(tst_main_window_plugin_pages_populated PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.1a T15: KF6MainWindow startup regression tests.
# Tests the three resolveStartupProfile() branches via the
# setProfileRegistryForTest + runStartupForTest seams added in T14.
# Needs the full link set because validLastActiveAutoLoads exercises
# loadProfile(), which instantiates PalmRuntime + AccountController.
add_executable(tst_kf6mainwindow_startup tst_kf6mainwindow_startup.cpp)
target_link_libraries(tst_kf6mainwindow_startup
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        KF6::XmlGui
        KF6::ConfigCore
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        PalmDeviceAccessLib
        WildPalmsRuntime
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_kf6mainwindow_startup COMMAND tst_kf6mainwindow_startup)
set_tests_properties(tst_kf6mainwindow_startup PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.1b T10: KF6MainWindow forget — integration tests + seams.
# Exercises onForgetProfile via QMetaObject::invokeMethod on private slots,
# with confirmForgetProfile overridden to skip the real dialog.
# Needs the full link set because forgetActiveProfileIsRejected exercises
# loadProfile(), which instantiates PalmRuntime + AccountController.
add_executable(tst_kf6mainwindow_forget_profile
    tst_kf6mainwindow_forget_profile.cpp)
target_link_libraries(tst_kf6mainwindow_forget_profile
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        KF6::XmlGui
        KF6::ConfigCore
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        PalmDeviceAccessLib
        WildPalmsRuntime
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_kf6mainwindow_forget_profile
         COMMAND tst_kf6mainwindow_forget_profile)
set_tests_properties(tst_kf6mainwindow_forget_profile PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.2 Task 2: KF6MainWindow conflict badge — TDD red-phase failing tests.
# Tests the future onConflictDetected slot, badge visibility, and count tracking.
add_executable(tst_kf6mainwindow_conflict_badge
    tst_kf6mainwindow_conflict_badge.cpp)
target_link_libraries(tst_kf6mainwindow_conflict_badge
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        KF6::XmlGui
        KF6::ConfigCore
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        PalmDeviceAccessLib
        WildPalmsRuntime
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_kf6mainwindow_conflict_badge
         COMMAND tst_kf6mainwindow_conflict_badge)
set_tests_properties(tst_kf6mainwindow_conflict_badge PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Shakedown F10: conflict resolutions applied in ConflictReviewDialog must
# land in the engine's SyncConflictStore (replay on next sync).
add_executable(tst_kf6mainwindow_conflict_apply
    tst_kf6mainwindow_conflict_apply.cpp)
target_link_libraries(tst_kf6mainwindow_conflict_apply
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        KF6::XmlGui
        KF6::ConfigCore
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        PalmDeviceAccessLib
        WildPalmsRuntime
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_kf6mainwindow_conflict_apply
         COMMAND tst_kf6mainwindow_conflict_apply)
set_tests_properties(tst_kf6mainwindow_conflict_apply PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Shakedown F14: completed runs stamp Profile::lastSyncTime; cancelled /
# failed runs must not.
add_executable(tst_kf6mainwindow_lastsync tst_kf6mainwindow_lastsync.cpp)
target_link_libraries(tst_kf6mainwindow_lastsync
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        KF6::XmlGui
        KF6::ConfigCore
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        PalmDeviceAccessLib
        WildPalmsRuntime
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_kf6mainwindow_lastsync COMMAND tst_kf6mainwindow_lastsync)
set_tests_properties(tst_kf6mainwindow_lastsync PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.1b T13: ProfilePropertiesDialog rename behavior — TDD red-phase tests.
# Tests the future renameRequested signal, Name field, and onApply slot.
add_executable(tst_profilepropertiesdialog_rename
    tst_profilepropertiesdialog_rename.cpp)
target_link_libraries(tst_profilepropertiesdialog_rename
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        KF6::I18n
        KF6::ConfigCore
        KF6::WidgetsAddons
        Kalburator::Sync
        WildPalmsCore
)
add_test(NAME tst_profilepropertiesdialog_rename
         COMMAND tst_profilepropertiesdialog_rename)
set_tests_properties(tst_profilepropertiesdialog_rename PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.1a T6: MassDeleteGuardPresenter — TDD red-phase failing tests.
# Tests the future IMassDeleteGuard::confirmMassDelete routing through promptUser.
add_executable(tst_massdeleteguardpresenter
    tst_massdeleteguardpresenter.cpp)
target_link_libraries(tst_massdeleteguardpresenter
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        KF6::I18n
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsCore
        WildPalmsRuntime
)
add_test(NAME tst_massdeleteguardpresenter
         COMMAND tst_massdeleteguardpresenter)
set_tests_properties(tst_massdeleteguardpresenter PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Sub-project E: mass-delete guard E2E — proves the guard fires against
# the original-destruction scenario (PC emptied between syncs).
# MockBlobBackend + BlobSyncBackendWrapper + MassDeleteGuardPresenter fixture.
# Needs WHOLE_ARCHIVE for blob DomainPlugin registrars (same as tst_palm_runtime_hotsync).
add_executable(tst_palm_mass_delete_guard_e2e
    tst_palm_mass_delete_guard_e2e.cpp)
target_link_libraries(tst_palm_mass_delete_guard_e2e
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_mass_delete_guard_e2e
         COMMAND tst_palm_mass_delete_guard_e2e)
set_tests_properties(tst_palm_mass_delete_guard_e2e PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M3 Tasks 14-17: fullSync / copyPalmToPC / copyPCToPalm / backup / restore
add_executable(tst_palm_runtime_modes tst_palm_runtime_modes.cpp)
target_link_libraries(tst_palm_runtime_modes
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime  # Phase J Task 7: palmruntime.cpp uses CalendarCollection_WP
        # Phase Ia.5: WHOLE_ARCHIVE pulls in libkalburator's static
        # DomainPlugin registrars so dispatchSync's
        # DomainRegistry::findByDomain() finds a plugin at sync time.
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_modes COMMAND tst_palm_runtime_modes)
set_tests_properties(tst_palm_runtime_modes PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# hub↔remote routing Task 4: per-route LC mapping assertions.
# Verifies that filtered routes (palm:<domain>/<slot>) materialize a
# wp-route-<id> FilteredCollectionBackend and pair route-<slotName>
# with the remote; wildcard routes bind Primary to wp-hub directly
# with no wrapper backend registered.
add_executable(tst_palm_runtime_routes tst_palm_runtime_routes.cpp)
target_link_libraries(tst_palm_runtime_routes
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime  # Phase J Task 7: palmruntime.cpp uses CalendarCollection_WP
        # Phase Ia.5: WHOLE_ARCHIVE pulls in libkalburator's static
        # DomainPlugin registrars so dispatchSync's
        # DomainRegistry::findByDomain() finds a plugin at sync time.
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_routes COMMAND tst_palm_runtime_routes)
set_tests_properties(tst_palm_runtime_routes PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# hub↔remote routing Task 5: end-to-end first-sync — Palm slot 3 record lands
# on mock remote WorkCal via Palm<->hub Star + hub-route<->remote Star.
add_executable(tst_palm_runtime_route_first_sync
    tst_palm_runtime_route_first_sync.cpp)
target_link_libraries(tst_palm_runtime_route_first_sync
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        WildPalmsPalmCalendar
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_route_first_sync
    COMMAND tst_palm_runtime_route_first_sync)
set_tests_properties(tst_palm_runtime_route_first_sync PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# hub↔remote routing Task 6: recategorization — record moves between route
# remotes when the hub canon categories field changes between hotSyncs.
# Two routes (Work slot 3, Home slot 4); first sync lands on WorkCal;
# mutating hub canon categories to ["Home"] + second sync moves it to HomeCal.
add_executable(tst_palm_runtime_route_recategorization
    tst_palm_runtime_route_recategorization.cpp)
target_link_libraries(tst_palm_runtime_route_recategorization
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        WildPalmsPalmCalendar
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_route_recategorization
    COMMAND tst_palm_runtime_route_recategorization)
set_tests_properties(tst_palm_runtime_route_recategorization PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M6b Task 3: PalmDeviceAccess connect lifecycle test
# Uses MockKPilotDeviceLink (subclass) injected via
# PalmDeviceAccess::setLinkFactoryForTest to drive
# onConnectionEstablished/onConnectionFailed without real hardware.
add_executable(tst_palm_device_access_connect
    tst_palm_device_access_connect.cpp
    mockkpilotdevicelink.cpp
)
target_link_libraries(tst_palm_device_access_connect
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime  # Phase J Task 7: palmruntime.cpp uses CalendarCollection_WP
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_device_access_connect COMMAND tst_palm_device_access_connect)
set_tests_properties(tst_palm_device_access_connect PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Phase J Task 9: CalDAV E2E integration test.
# Includes the libkalburator FakeCalDavServer source directly.
add_executable(tst_runtime_caldav_e2e
    tst_runtime_caldav_e2e.cpp
    ${WILDPALMS_LIBKALBURATOR_DIR}/tests/sync/fakecaldavserver.cpp
)
target_include_directories(tst_runtime_caldav_e2e
    PRIVATE
        ${WILDPALMS_LIBKALBURATOR_DIR}/tests/sync
)
target_link_libraries(tst_runtime_caldav_e2e
    PRIVATE
        Qt::Core
        Qt::Network
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime  # Phase J Task 7: palmruntime.cpp uses CalendarCollection_WP
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_runtime_caldav_e2e COMMAND tst_runtime_caldav_e2e)
set_tests_properties(tst_runtime_caldav_e2e PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Phase J Task 11: CardDAV E2E integration test (contacts equivalent of caldav).
# Includes the libkalburator FakeCardDavServer source directly.
add_executable(tst_runtime_carddav_e2e
    tst_runtime_carddav_e2e.cpp
    ${WILDPALMS_LIBKALBURATOR_DIR}/tests/sync/fakecarddavserver.cpp
)
target_include_directories(tst_runtime_carddav_e2e
    PRIVATE
        ${WILDPALMS_LIBKALBURATOR_DIR}/tests/sync
)
target_link_libraries(tst_runtime_carddav_e2e
    PRIVATE
        Qt::Core
        Qt::Network
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_runtime_carddav_e2e COMMAND tst_runtime_carddav_e2e)
set_tests_properties(tst_runtime_carddav_e2e PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Phase J Task 10: PalmRuntime stress test — 10 hotSync iterations, no leak.
add_executable(tst_runtime_stress tst_runtime_stress.cpp)
target_link_libraries(tst_runtime_stress
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_runtime_stress COMMAND tst_runtime_stress)
set_tests_properties(tst_runtime_stress PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.1d T1 — ProfileRegistry serial binding
add_executable(tst_profileregistry_serial_binding tst_profileregistry_serial_binding.cpp)
target_link_libraries(tst_profileregistry_serial_binding
    PRIVATE
        Qt::Core
        Qt::Test
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsCore
        WildPalmsRuntime
        PalmDeviceAccessLib
        WildPalmsPalmDevice
        KF6::I18n
        KF6::ConfigCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_profileregistry_serial_binding COMMAND tst_profileregistry_serial_binding)
set_tests_properties(tst_profileregistry_serial_binding PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.1d T2 — DeviceFingerprint::compare()
add_executable(tst_devicefingerprint_compare tst_devicefingerprint_compare.cpp)
target_link_libraries(tst_devicefingerprint_compare
    PRIVATE
        Qt::Core Qt::Test
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsCore
        WildPalmsRuntime
        WildPalmsPalmDevice
        KF6::I18n
        pisock bluetooth usb
)
add_test(NAME tst_devicefingerprint_compare COMMAND tst_devicefingerprint_compare)
set_tests_properties(tst_devicefingerprint_compare PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.1d T7 — mismatch dialog
add_executable(tst_kf6mainwindow_mismatch_dialog tst_kf6mainwindow_mismatch_dialog.cpp)
target_link_libraries(tst_kf6mainwindow_mismatch_dialog
    PRIVATE
        Qt::Core Qt::Test Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsCore
        WildPalmsRuntime
        WildPalmsPalmDevice
        PalmDeviceAccessLib
        KF6::I18n KF6::ConfigCore KF6::XmlGui
        pisock bluetooth usb
)
add_test(NAME tst_kf6mainwindow_mismatch_dialog COMMAND tst_kf6mainwindow_mismatch_dialog)
set_tests_properties(tst_kf6mainwindow_mismatch_dialog PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.3 T1 — Profile category-slot snapshot persistence
add_executable(tst_profile_category_snapshot tst_profile_category_snapshot.cpp)
target_link_libraries(tst_profile_category_snapshot
    PRIVATE
        Qt::Core
        Qt::Test
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsCore
        WildPalmsRuntime
        PalmDeviceAccessLib
        WildPalmsPalmDevice
        KF6::I18n
        KF6::ConfigCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_profile_category_snapshot COMMAND tst_profile_category_snapshot)
set_tests_properties(tst_profile_category_snapshot PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# F.3 T7 — Sync mappings graph view
add_executable(tst_syncmappingsgraphview tst_syncmappingsgraphview.cpp)
target_link_libraries(tst_syncmappingsgraphview
    PRIVATE
        Qt::Core Qt::Test Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsCore
        WildPalmsAppMapping
        WildPalmsRuntime
        WildPalmsPalmDevice
        PalmDeviceAccessLib
        KF6::I18n KF6::ConfigCore
        pisock bluetooth usb
)
add_test(NAME tst_syncmappingsgraphview COMMAND tst_syncmappingsgraphview)
set_tests_properties(tst_syncmappingsgraphview PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Sync Patchbay (Part 1) — pure-data model tests (no graphics).
add_executable(tst_patchbay_model tst_patchbay_model.cpp)
target_link_libraries(tst_patchbay_model
    PRIVATE
        Qt::Core Qt::Test Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsCore
        WildPalmsAppPatchbay
        WildPalmsRuntime
        WildPalmsPalmDevice
        PalmDeviceAccessLib
        KF6::I18n KF6::ConfigCore
        pisock bluetooth usb
)
add_test(NAME tst_patchbay_model COMMAND tst_patchbay_model)
set_tests_properties(tst_patchbay_model PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Sync Patchbay (Part 1) — view tests (real Graffodil scene).
add_executable(tst_patchbay_view tst_patchbay_view.cpp)
target_link_libraries(tst_patchbay_view
    PRIVATE
        Qt::Core Qt::Test Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsCore
        WildPalmsAppPatchbay
        WildPalmsRuntime
        WildPalmsPalmDevice
        PalmDeviceAccessLib
        KF6::I18n KF6::ConfigCore
        pisock bluetooth usb
)
add_test(NAME tst_patchbay_view COMMAND tst_patchbay_view)
set_tests_properties(tst_patchbay_view PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Sub-project D Task 3: PalmRuntime::syncCompleted signal fires once per sync run.
add_executable(tst_palm_runtime_emits_sync_completed
    tst_palm_runtime_emits_sync_completed.cpp)
target_link_libraries(tst_palm_runtime_emits_sync_completed
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_emits_sync_completed
    COMMAND tst_palm_runtime_emits_sync_completed)
set_tests_properties(tst_palm_runtime_emits_sync_completed PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Shakedown F3/F11: runStarted/runFinished pairing on the early-return
# paths + re-entrant sync rejection (isSyncRunning).
add_executable(tst_palm_runtime_run_lifecycle
    tst_palm_runtime_run_lifecycle.cpp)
target_link_libraries(tst_palm_runtime_run_lifecycle
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_run_lifecycle
    COMMAND tst_palm_runtime_run_lifecycle)
set_tests_properties(tst_palm_runtime_run_lifecycle PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Clobber-sync Task 10: PalmRuntime::clobberSync entry point.
# Smokes the SyncEngine dispatch path with ExecutionOverride::clobber=true.
# Mirrors tst_palm_runtime_modes' link set (full Palm stack + WHOLE_ARCHIVE
# Kalburator::Sync so the static DomainPlugin registrars are pulled in).
add_executable(tst_palm_runtime_clobber_sync tst_palm_runtime_clobber_sync.cpp)
target_link_libraries(tst_palm_runtime_clobber_sync
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_clobber_sync COMMAND tst_palm_runtime_clobber_sync)
set_tests_properties(tst_palm_runtime_clobber_sync PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Clobber-sync Task 4: ClobberDialog modal — per-conduit checkboxes drive
# selectedMappingIds(). Uses QTEST_MAIN (full QApplication) because the
# dialog needs Qt::Widgets.
add_executable(tst_clobber_dialog tst_clobber_dialog.cpp)
target_link_libraries(tst_clobber_dialog
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        PalmDeviceAccessLib
        WildPalmsRuntime
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_clobber_dialog COMMAND tst_clobber_dialog)
set_tests_properties(tst_clobber_dialog PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Accounts-first wizard — AccountsSetupPage unit test
add_executable(tst_accountssetuppage tst_accountssetuppage.cpp)
target_link_libraries(tst_accountssetuppage
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsAppWizard
        WildPalmsAppAccounts
        WildPalmsCore
        WildPalmsPalmDevice
        KF6::I18n
        KF6::ConfigCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_accountssetuppage COMMAND tst_accountssetuppage)
set_tests_properties(tst_accountssetuppage PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Substrate A3: translateRouteSpec — names-first translator + per-route status.
# (Replaces the old tst_route_mapping, which exercised the slot-form +
# stores-hash signature that substrate A3 retired.) Links WildPalmsRuntime +
# Kalburator::Sync directly (not WildPalmsCore) so that #include "synctypes.h"
# resolves to libkalburator's Kalburator::Sync header rather than the WP-local
# src/core/synctypes.h (which WildPalmsCore's PUBLIC include path would shadow).
add_executable(tst_routemapping tst_routemapping.cpp)
target_link_libraries(tst_routemapping
    PRIVATE
        Qt::Core
        Qt::Test
        WildPalmsRuntime
        Kalburator::Sync
        WildPalmsPalmCalendar
)
# Add src/ so that #include "palm/calendar/..." and "runtime/..." resolve.
# Prepend libkalburator/src/types so that #include "synctypes.h" finds
# Kalburator::Sync::SyncMapping before the WP-local src/core/synctypes.h
# that WildPalmsCore's PUBLIC include path would otherwise win with.
target_include_directories(tst_routemapping BEFORE PRIVATE
    ${WILDPALMS_LIBKALBURATOR_DIR}/src/types
    ${CMAKE_SOURCE_DIR}/src
)
add_test(NAME tst_routemapping COMMAND tst_routemapping)
set_tests_properties(tst_routemapping PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Substrate A1: conduit descriptor contract (PimPlugin virtuals + default
# matchesCollection). Absorbs the old tst_domainfilter matching cases.
# Pure-logic test — mirrors tst_route_mapping's lean link set (the descriptor
# base lives in WildPalmsRuntime; categoryStore() touches WildPalmsPalmCalendar).
add_executable(tst_conduit_descriptor tst_conduit_descriptor.cpp)
target_link_libraries(tst_conduit_descriptor
    PRIVATE
        Qt::Core
        Qt::Test
        WildPalmsRuntime
        Kalburator::Sync
        WildPalmsPalmCalendar
)
target_include_directories(tst_conduit_descriptor BEFORE PRIVATE
    ${WILDPALMS_LIBKALBURATOR_DIR}/src/types
    ${CMAKE_SOURCE_DIR}/src
)
add_test(NAME tst_conduit_descriptor COMMAND tst_conduit_descriptor)
set_tests_properties(tst_conduit_descriptor PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Substrate A3: CategoryReconciler — pure name->slot binding over AppInfo bytes.
# Uses CategoryInfo (WildPalmsCore, which resolves pack/unpack_CategoryAppInfo
# from pisock internally); reconcileCategories lives in WildPalmsRuntime.
add_executable(tst_category_reconciler tst_category_reconciler.cpp)
target_link_libraries(tst_category_reconciler
    PRIVATE
        Qt::Core
        Qt::Test
        WildPalmsRuntime
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_category_reconciler COMMAND tst_category_reconciler)
set_tests_properties(tst_category_reconciler PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Substrate A2: LocalFolderProvider/Contribution — credential-less source.
# Provider lives in PalmDeviceAccessLib (next to standardcontributions); the
# sinks (RawFiles/Markdown) + IProvider come from Kalburator::Sync.
add_executable(tst_localfolder_provider tst_localfolder_provider.cpp)
target_link_libraries(tst_localfolder_provider
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        PalmDeviceAccessLib
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsCore
        WildPalmsPalmDevice
        pisock
        bluetooth
        usb
)
add_test(NAME tst_localfolder_provider COMMAND tst_localfolder_provider)
set_tests_properties(tst_localfolder_provider PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Substrate A1 acceptance: a fifth conduit participates everywhere a stock one
# does, with zero WP source change beyond registration. Constructs a real
# PalmRuntime (full Palm stack + WHOLE_ARCHIVE Kalburator::Sync for the static
# DomainPlugin registrars) and uses translateRouteSpec / SyncMapping directly,
# so prepend libkalburator/src/types for the canonical synctypes.h.
add_executable(tst_fifth_conduit tst_fifth_conduit.cpp)
target_link_libraries(tst_fifth_conduit
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsRuntime
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
target_include_directories(tst_fifth_conduit BEFORE PRIVATE
    ${WILDPALMS_LIBKALBURATOR_DIR}/src/types
    ${CMAKE_SOURCE_DIR}/src
)
add_test(NAME tst_fifth_conduit COMMAND tst_fifth_conduit)
set_tests_properties(tst_fifth_conduit PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
