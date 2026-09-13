# WildPalmsAppWizard — F.1c.1 NewProfileWizard
#
# Lives in a separate static lib (matching WildPalmsAppAccounts etc.) so
# Resolve shared sync types from libkalburator's public target include path.

add_library(WildPalmsAppWizard STATIC
    wizardstate.h
    newprofilewizard.h
    newprofilewizard.cpp
    namepage.h
    namepage.cpp
    accountssetuppage.h
    accountssetuppage.cpp
    targetpickerrow.h
    targetpickerrow.cpp
    targetpickerpage.h
    targetpickerpage.cpp
    reviewpage.h
    reviewpage.cpp
)

set_target_properties(WildPalmsAppWizard PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(WildPalmsAppWizard
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsAppWizard
    PUBLIC
        Qt::Core
        Qt::Widgets
        Kalburator::Sync
        WildPalmsAppAccounts
        PalmDeviceAccessLib
)
