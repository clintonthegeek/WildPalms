# WildPalmsCore library - Core functionality that will be reused by main app and tests

add_library(WildPalmsCore SHARED
    wildpalms_core.cpp
    wildpalms_version.h

    # Plugin ABI interfaces (Q_OBJECT meta-object for ActionContext)
    core/ipluginaction.h
    core/ipluginaction.cpp
    # K.8b T13: IBackendPlugin V1 (ibackendplugin.h/cpp) and V2
    # (ibackendplugin_v2.h) deleted along with BackendPluginManager. The
    # five Palm plugins migrated to Kalburator::Plugin in K.8b T1-T5.

    # Application UI
    app/logwidget.cpp
    app/logwidget.h
    app/conflictdialog.cpp
    app/conflictdialog.h
    app/conflictreviewwidget.cpp
    app/conflictreviewwidget.h
    # K.8b T13: Native InteractiveConflictHandler deleted; superseded by
    # KalburatorInteractiveConflictHandler in src/app/conflict/.

    # Settings and dialogs
    settingsdialog.cpp
    settingsdialog.h
    profile.cpp
    profile.h

    # Palm device communication
    palm/kpilotlink.cpp
    palm/kpilotlink.h
    palm/pilotrecord.cpp
    palm/pilotrecord.h
    palm/kpilotdevicelink.cpp
    palm/kpilotdevicelink.h
    palm/categoryinfo.cpp
    palm/categoryinfo.h
    palm/palmdevicemonitor.cpp
    palm/palmdevicemonitor.h

    # JSON hash store for per-conduit pending-conflict tracking (legacy code
    # path used by SyncState::pendingConflictCount). Lives alongside, not
    # replacing, the SQLite-backed Kalburator::Storage::BaselineStore used by
    # the new sync engine in palmruntime.cpp.
    sync/journal/journalcommon.h
    sync/journal/baselinestore.cpp
    sync/journal/baselinestore.h
    sync/journal/idmappingstore.cpp
    sync/journal/idmappingstore.h

    # Sync engine (Phase 4)
    # K.8b T13: SyncEngine + LocalFileBackend + SyncConduitBase (conduit.{h,cpp})
    # deleted. SyncState retained — still used by kf6mainwindow.cpp (T14
    # markers) and tests/test_syncstate.cpp. syncbackend.h kept until T14
    # cleanup confirms no remaining consumers.
    sync/syncstate.cpp
    sync/syncstate.h
    sync/syncbackend.h

    # KF6 integration
    kf6/actionmanager.cpp
    kf6/actionmanager.h
    kf6/profilemenucontroller.cpp
    kf6/profilemenucontroller.h
    kf6/autosyncorchestrator.cpp
    kf6/autosyncorchestrator.h
    # K.8b T13: ConduitManager deleted (plugin loop removed in T6; only
    # kf6mainwindow had remaining references, now T14-marked).
    kf6/kf6mainwindow.cpp
    kf6/kf6mainwindow.h
    kf6/kf6settings.cpp
    kf6/kf6settings.h

    # Phase E.16 — pilot-link factory impl. Lives in WildPalmsCore (not
    # WildPalmsRuntime) because Core PRIVATE-links WildPalmsPalmDevice
    # without creating an AUTOMOC cycle (PalmDevice's Core link is
    # PRIVATE on its end). Header in src/runtime/.
    runtime/pilotlinkconnectionfactory.cpp

    # Widgets - Common
    widgets/common/categorymanager.cpp
    widgets/common/categorymanager.h
    widgets/common/categorymodel.cpp
    widgets/common/categorymodel.h
    widgets/common/categoryfilterwidget.cpp
    widgets/common/categoryfilterwidget.h

    # Widgets - Dashboard
    widgets/dashboard/dashboardwidget.cpp
    widgets/dashboard/dashboardwidget.h
    widgets/dashboard/syncstatusmodel.cpp

    # Widgets - Dialogs
    widgets/dialogs/categoryeditordialog.cpp
    widgets/dialogs/categoryeditordialog.h
    widgets/dialogs/conflictreviewdialog.cpp
    widgets/dialogs/conflictreviewdialog.h
    widgets/dialogs/profilepropertiesdialog.cpp
    widgets/dialogs/profilepropertiesdialog.h
    widgets/dialogs/taskeditdialog.cpp
    widgets/dialogs/taskeditdialog.h

    # Widgets - Sidebar
    widgets/sidebar/profilesidebar.cpp
    widgets/sidebar/profilesidebar.h

    # Persistent host adapter for libkalburator secret references.
    security/kwalletsecretstore.h security/kwalletsecretstore.cpp
)

target_link_libraries(WildPalmsCore
    PUBLIC
        Qt::Core
        Qt::Widgets
        KF6::I18n
    PRIVATE
        Kalburator::Sync
        Qt::Network
        KF6::CalendarCore
        KF6::CoreAddons
        KF6::XmlGui
        KF6::WidgetsAddons
        KF6::ConfigCore
        KF6::ConfigWidgets
        KF6::Notifications
        KF6::StatusNotifierItem
        KF6::Wallet
        pisock
        bluetooth  # pilot-link transitive dep (Bluetooth HotSync)
        usb        # pilot-link transitive dep (USB HotSync)
        ${UDEV_LIBRARIES}
        # Phase E.16 — pilot-link wrappers used by kf6mainwindow's
        # device-connect path. PalmDevice→Core is PRIVATE (demoted)
        # so the link is one-directional from Core's perspective.
        WildPalmsPalmDevice
        WildPalmsPalmSync
        # M2 Task 11 — PalmRuntime lives in this static lib until Task 12
        # merges it back into WildPalmsRuntime.
        PalmDeviceAccessLib
        # Phase E.9 — new-ABI plugin manager used by KF6MainWindow.
        # WildPalmsRuntime PUBLIC-links WildPalmsCore; linking it PRIVATE
        # here embeds its static objects without creating a consumer
        # cycle.
        # Phase J Task 7: PalmDeviceAccessLib must come BEFORE WildPalmsRuntime
        # so that palmruntime.cpp.o's reference to CalendarCollection_WP
        # is satisfied by WildPalmsRuntime when the linker processes the
        # archives in order (single-pass rule).
        WildPalmsRuntime
        # M5a — libkalburator-side interactive ConflictHandler. Separate
        # static lib to avoid the QSYNCCORE_CONFLICTPOLICY_H header-guard
        # collision between libkalburator's conflict/conflictpolicy.h and
        # WildPalms' local sync/qsynccore/conflictpolicy.h.
        WildPalmsAppConflict
        # F.3 — Sync Mappings graph view (PalmDbNode/ProviderNode/
        # MappingEdge/SyncMappingGraphView/MappingInspectorPanel/
        # SyncMappingsPage). Separate static lib so its TUs see
        # libkalburator's synctypes.h (not WP-local core/synctypes.h).
        WildPalmsAppMapping
        # Sync Patchbay (Part 1) — three-tier graph centerpiece. Separate
        # static library, keeping mapping UI dependencies isolated.
        WildPalmsAppPatchbay
        # Phase Ic — AccountsPage + AddAccountDialog.
        # SettingsDialog instantiates AccountsPage when AccountController +
        # PalmRuntime are supplied; without this link the symbols are missing.
        WildPalmsAppAccounts
        # F.1c.1 — NewProfileWizard. KF6MainWindow::onNewProfile() invokes it.
        WildPalmsAppWizard
        # K.8b T6: five static Palm plugins loaded in-process by
        # PalmRuntime::registerPalmPlugins(). Linked here into WildPalmsCore.so
        # so that palmruntime.o (in PalmDeviceAccessLib, also PRIVATE here) can
        # resolve plugin constructor symbols at the shared-lib link step.
        # Note: WildPalmsCore was removed from the plugin statics' own
        # target_link_libraries to break the CMake autogen cycle; symbols from
        # WildPalmsCore that the plugins need are satisfied at this same link step.
        wildpalms_calendar_static
        wildpalms_contacts_static
        wildpalms_memo_static
        wildpalms_todos_static
)

target_include_directories(WildPalmsCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sync>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/palm>
    $<INSTALL_INTERFACE:include/wildpalms>
    $<INSTALL_INTERFACE:include/wildpalms/core>
    $<INSTALL_INTERFACE:include/wildpalms/sync>
    $<INSTALL_INTERFACE:include/wildpalms/palm>
    $<INSTALL_INTERFACE:include/wildpalms/pilot-link>
)

target_include_directories(WildPalmsCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/kf6
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets
    ${UDEV_INCLUDE_DIRS}
)

set_target_properties(WildPalmsCore PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME Core
)

# For development builds: tell the app where to find wildpalmsui.rc
# and Plucker data without requiring a full install.
# Installed/AppImage builds set -DWILDPALMS_INSTALLED=ON so the app
# uses standard KDE resource lookup instead.
if(NOT WILDPALMS_INSTALLED)
    target_compile_definitions(WildPalmsCore PRIVATE
        WILDPALMS_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
    )
endif()

# Install the shared library with export
install(TARGETS WildPalmsCore
    EXPORT WildPalmsTargets
    LIBRARY DESTINATION lib
)

# Install public SDK headers
# K.8b T13: iconduit/isyncconduit/itoolconduit/ibackendplugin deleted.
install(FILES
    core/iplugin.h
    core/ipluginaction.h
    DESTINATION include/wildpalms/core
)

# K.8b T13: conduit.h (SyncConduitBase) deleted.
install(FILES
    sync/syncstate.h
    sync/syncbackend.h
    DESTINATION include/wildpalms/sync
)

install(FILES
    sync/qsynccore/synccommon.h
    sync/qsynccore/idmappingstore.h
    sync/qsynccore/baselinestore.h
    sync/qsynccore/conflictrecord.h
    sync/qsynccore/conflictstore.h
    sync/qsynccore/conflictpolicy.h
    DESTINATION include/wildpalms/sync/qsynccore
)

install(FILES
    palm/pilotrecord.h
    palm/categoryinfo.h
    palm/kpilotlink.h
    DESTINATION include/wildpalms/palm
)

# Ensure pilot-link is built before WildPalmsCore
add_dependencies(WildPalmsCore pilot-link-external)

# Plugin subdirectory (conduit plugins that link against WildPalmsCore)
add_subdirectory(plugins)

# Palm-side Kalburator::Sync integration (Phase E.3+ of libkalburator
# integration). PalmBackend : IBlobBackend + device-access abstraction.
# Does not link pisock; the real pilot-link adapter lands in E.4.
add_subdirectory(palm/sync)

# Pilot-link adapter for IPalmDatabaseAccess (Phase E.4 of libkalburator
# integration). Bridges WildPalmsCore (pisock) and WildPalmsPalmSync
# (Kalburator::Sync).
add_subdirectory(palm/device)

# Palm-aware ConflictHandler + BackendConfig (Phase E.5 of libkalburator
# integration). Registers under backend id "palm" in
# Kalburator::Sync::QSyncCore::ConflictHandlerRegistry.
add_subdirectory(palm/conflict)

# Calendar-typed SyncBackend adapter for Palm Datebook (Phase E.6 of
# libkalburator integration). Exposes virtual sub-calendars per Palm
# category slot via PalmCalendarBackend : SyncBackend.
add_subdirectory(palm/calendar)

# Typed adapters for Palm Contacts/Memos/Todos (Phase E.7 of libkalburator
# integration). Encode/decode PODs to/from KDE PIM types (Addressee, Event, Todo).
add_subdirectory(palm/codecs)

# Stateless typed wrappers over PalmBackend for WP-internal UI consumption
# (Phase E.7 of libkalburator integration). Not on the plugin ABI.
add_subdirectory(palm/adapters)

# G.7 — SyncBackend implementations for Palm non-calendar databases.
# Each wraps IPalmDatabaseAccess and is routed via dispatchBlobSync / blobBatchDiff.
add_subdirectory(palm/contacts)
add_subdirectory(palm/memo)
add_subdirectory(palm/todo)
add_subdirectory(palm/transformations)

# Phase Ic — AccountsPage + AddAccountDialog.
# Same pattern as WildPalmsAppConflict / WildPalmsAppMapping: separate lib so
# libkalburator headers resolve to Kalburator::Sync (not WP-local Sync::).
add_subdirectory(app/accounts)

# M5a — libkalburator-side interactive ConflictHandler. Separate lib to
# avoid the QSYNCCORE_CONFLICTPOLICY_H header-guard collision between
# libkalburator's conflict/conflictpolicy.h and WildPalms' local
# sync/qsynccore/conflictpolicy.h.
add_subdirectory(app/conflict)

# F.3 — Sync Mappings graph view (WildPalmsAppMapping).
# Separate static lib for the same reason as WildPalmsAppConflict: avoids
# having WildPalmsCore's PUBLIC src/core/ on the include path so that
# canonical libkalburator sync types resolve through the public target include path
# header (not WP-local Sync:: namespace).
add_subdirectory(app/mapping)

# Sync Patchbay (Part 1) — three-tier Graffodil graph (WildPalmsAppPatchbay).
# Keep this optional UI component's dependency surface isolated.
add_subdirectory(app/patchbay)

# F.1c.1 — NewProfileWizard.
add_subdirectory(app/wizard)

# Plugin runtime — managers, simple action context, install source
# collector, and (since E.15b) the Full-Sync host-interface
# implementations consumed by libkalburator.
add_subdirectory(runtime)
