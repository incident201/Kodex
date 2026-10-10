# SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.28)

set(KODEX_VERSION "0.1.0")
project(Kodex VERSION ${KODEX_VERSION} LANGUAGES CXX)

set(QT_MIN_VERSION "6.11.0")
set(KF_MIN_VERSION "6.30.0")
set(KODEX_APP_ID "io.github.incident201.kodex")

# Optional local copy of extra-cmake-modules, see scripts/bootstrap-ecm.sh.
if(EXISTS "${CMAKE_SOURCE_DIR}/.deps/usr")
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/.deps/usr")
endif()
find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR})

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(KDEInstallDirs6)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
# The code builds QString and QJsonObject values from plain string literals in many places;
# the other strict Qt definitions of KDECompilerSettings stay enabled.
remove_definitions(-DQT_NO_CAST_FROM_ASCII -DQT_NO_CAST_TO_ASCII -DQT_USE_QSTRINGBUILDER)
# The executables stay in the top of the build directory: an uninstalled Kodex finds its
# translations in <build>/locale, next to the executable.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
include(KDEClangFormat)
include(ECMSetupVersion)
include(ECMInstallIcons)
include(FeatureSummary)
include(CTest)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Network Test)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS CoreAddons Config I18n Kirigami Wallet SyntaxHighlighting)
qt_standard_project_setup(REQUIRES ${QT_MIN_VERSION})

ecm_setup_version(${KODEX_VERSION}
    VARIABLE_PREFIX KODEX
    VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/kodex_version.h)

add_library(kodex_core STATIC
    src/settings.cpp src/rpcclient.cpp src/listmodel.cpp src/gitservice.cpp
    src/maintenance.cpp src/appcontroller.cpp src/richtext.cpp)
target_include_directories(kodex_core PUBLIC src ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(kodex_core PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network
    KF6::ConfigCore KF6::I18n KF6::Wallet KF6::SyntaxHighlighting)

qt_add_executable(kodex src/main.cpp)
qt_add_qml_module(kodex URI io.github.incident201.kodex VERSION 1.0
    QML_FILES qml/Main.qml qml/SettingsPage.qml qml/ProgramField.qml qml/SlimScrollBar.qml
              qml/SlimScrollView.qml qml/DialogButtons.qml qml/RequestCard.qml qml/GitPanel.qml qml/InlineSelector.qml)
target_link_libraries(kodex PRIVATE kodex_core Qt6::Widgets Qt6::Quick Qt6::QuickControls2
    KF6::Kirigami KF6::I18nQml KF6::CoreAddons)
install(TARGETS kodex ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
file(GLOB KODEX_ICONS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/*-apps-${KODEX_APP_ID}.png)
ecm_install_icons(ICONS ${KODEX_ICONS} DESTINATION ${KDE_INSTALL_ICONDIR} THEME hicolor)

# Translations: po/<lang>/kodex.po, installed as kodex.mo by ki18n.
ki18n_install(po)

# The desktop entry and the AppStream metadata are translated from the same catalogs.
# msgfmt expects <dir>/<lang>.po and <dir>/LINGUAS, so the catalogs are staged that way.
find_program(KODEX_MSGFMT msgfmt REQUIRED)
set(KODEX_PO_STAGE ${CMAKE_CURRENT_BINARY_DIR}/po-stage)
file(GLOB KODEX_PO_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/po/*/kodex.po)
set(KODEX_LINGUAS)
foreach(po_file IN LISTS KODEX_PO_FILES)
    get_filename_component(po_dir ${po_file} DIRECTORY)
    get_filename_component(po_lang ${po_dir} NAME)
    configure_file(${po_file} ${KODEX_PO_STAGE}/${po_lang}.po COPYONLY)
    list(APPEND KODEX_LINGUAS ${po_lang})
endforeach()
list(JOIN KODEX_LINGUAS "\n" KODEX_LINGUAS_TEXT)
file(WRITE ${KODEX_PO_STAGE}/LINGUAS "${KODEX_LINGUAS_TEXT}\n")

set(KODEX_DESKTOP_FILE ${CMAKE_CURRENT_BINARY_DIR}/${KODEX_APP_ID}.desktop)
set(KODEX_METAINFO_FILE ${CMAKE_CURRENT_BINARY_DIR}/${KODEX_APP_ID}.metainfo.xml)
add_custom_command(OUTPUT ${KODEX_DESKTOP_FILE}
    COMMAND ${KODEX_MSGFMT} --desktop --template=${CMAKE_CURRENT_SOURCE_DIR}/data/${KODEX_APP_ID}.desktop
            -d ${KODEX_PO_STAGE} -o ${KODEX_DESKTOP_FILE}
    DEPENDS data/${KODEX_APP_ID}.desktop ${KODEX_PO_FILES}
    VERBATIM)
add_custom_command(OUTPUT ${KODEX_METAINFO_FILE}
    COMMAND ${KODEX_MSGFMT} --xml --template=${CMAKE_CURRENT_SOURCE_DIR}/data/${KODEX_APP_ID}.metainfo.xml
            -d ${KODEX_PO_STAGE} -o ${KODEX_METAINFO_FILE}
    DEPENDS data/${KODEX_APP_ID}.metainfo.xml ${KODEX_PO_FILES}
    VERBATIM)
add_custom_target(kodex_metadata ALL DEPENDS ${KODEX_DESKTOP_FILE} ${KODEX_METAINFO_FILE})
install(FILES ${KODEX_DESKTOP_FILE} DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES ${KODEX_METAINFO_FILE} DESTINATION ${KDE_INSTALL_METAINFODIR})

if(BUILD_TESTING)
    find_program(KODEX_DESKTOP_FILE_VALIDATE desktop-file-validate)
    if(KODEX_DESKTOP_FILE_VALIDATE)
        add_test(NAME desktop-file COMMAND ${KODEX_DESKTOP_FILE_VALIDATE} ${KODEX_DESKTOP_FILE})
    endif()
    find_program(KODEX_APPSTREAMCLI appstreamcli)
    if(KODEX_APPSTREAMCLI)
        add_test(NAME metainfo COMMAND ${KODEX_APPSTREAMCLI} validate --no-net ${KODEX_METAINFO_FILE})
    endif()

    qt_add_executable(kodex_tests tests/test_core.cpp)
    target_link_libraries(kodex_tests PRIVATE kodex_core Qt6::Test)
    target_compile_definitions(kodex_tests PRIVATE SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME core COMMAND kodex_tests)
    set_tests_properties(core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # These two need an installed Codex and are run by hand.
    qt_add_executable(kodex_live_tests tests/test_live.cpp)
    target_link_libraries(kodex_live_tests PRIVATE kodex_core Qt6::Test)
    qt_add_executable(kodex_interactive_tests tests/test_interactive.cpp)
    target_compile_definitions(kodex_interactive_tests PRIVATE SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(kodex_interactive_tests PRIVATE kodex_core Qt6::Test Qt6::Widgets Qt6::Quick
        Qt6::QuickControls2 KF6::Kirigami KF6::I18nQml KF6::CoreAddons)
endif()

file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES CONFIGURE_DEPENDS src/*.cpp src/*.h tests/*.cpp)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
