cmake_minimum_required(VERSION 3.20)
project(trails VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Faster rebuilds when available -- found, never required.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER   ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()
find_program(MOLD_PROGRAM mold)
if(MOLD_PROGRAM)
    add_link_options(-fuse-ld=mold)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-4.0)
find_package(spdlog REQUIRED)
pkg_check_modules(SQLITE REQUIRED sqlite3)   # s009 (Fedora: sqlite-devel, Ubuntu: libsqlite3-dev)

# ── trails_core -- the GTK-free core (the model side of the one real seam) ──────
# No gtkmm on this library's link line, BY DESIGN: a stray UI include under
# core/ fails to compile, so the seam is enforced by the build graph and the
# core stays headless-testable. (docs/two-layer-seam.md.)
add_library(trails_core STATIC
    src/core/Recents.cpp
    src/core/TextMap.cpp
    # The SHORTCUT-REGISTRY paradigm: one list, two consumers (App wires accels
    # from it, ShortcutsDialog renders from it) so a key and its advertised row
    # can't drift. GTK-free, so it lives in core and the selftest guards it
    # headless -- a pure test proves no two chords collide.
    src/core/Shortcuts.cpp
    # The UNDO paradigm (s046): core/Undo.hpp is the journal (header-only, a
    # template over the app's store); Stones is the demo model + its verbs +
    # the verb table the selftest gate walks. docs/undo.md.
    src/core/Stones.cpp
    # s002 -- the sampler core. probe/ readers each parse one /proc or /sys
    # source under a ROOT path (fixture trees in the selftest); Sampler owns
    # the thread + rings and publishes an immutable Snapshot the UI reads
    # without locking. include/core/series/Ring.hpp is header-only.
    src/core/probe/Stat.cpp
    src/core/probe/Meminfo.cpp
    src/core/probe/NetDev.cpp
    src/core/probe/DiskStats.cpp
    src/core/probe/ProcTable.cpp
    src/core/probe/CpuInfo.cpp
    src/core/probe/Swap.cpp
    # s007 -- mounts / statvfs / sys/block, and sys/class/net / wireless / addrs.
    src/core/probe/Storage.cpp
    src/core/probe/Links.cpp
    # s008 -- hwmon, power_supply, drm cards + fdinfo engine time.
    src/core/probe/Sensors.cpp
    src/core/probe/Gpu.cpp
    src/core/Sampler.cpp
    # s003 -- the one tab list (rail, pages, icons, Alt+N) and value formatting.
    src/core/Tabs.cpp
    src/core/Format.cpp
    # s005 -- the Processes tab's GTK-free half: tree index, filter, names.
    src/core/ProcView.cpp
    # s006 -- process actions: the knob store the journal writes through, the
    # syscalls (SysProcOS) and the verb table the selftest gate walks.
    src/core/ProcActs.cpp
    # s009 -- the recorder (SQLite writer thread) and the app-wide store the one
    # journal writes through (process knobs + marks).
    src/core/Recorder.cpp
    src/core/AppStore.cpp
)
target_include_directories(trails_core PUBLIC include)
target_include_directories(trails_core PRIVATE ${SQLITE_INCLUDE_DIRS})
target_link_libraries(trails_core PUBLIC ${SQLITE_LIBRARIES})
target_link_directories(trails_core PUBLIC ${SQLITE_LIBRARY_DIRS})
# Vendored nlohmann/json (header-only, MIT), PRIVATE -- an implementation detail
# of the pump, no consumer inherits the include (CANON: surprise-free build).
target_include_directories(trails_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# ── Headless selftest -- exercises the core with no display ────────────────────
add_executable(trails_selftest src/selftest.cpp src/selftest_sampler.cpp src/selftest_procacts.cpp src/selftest_storage.cpp src/selftest_sensors.cpp src/selftest_recorder.cpp)
find_package(Threads REQUIRED)

# ── trails-helper -- the one thing that runs as root (s006) ────────────────────
# pkexec runs it for actions on processes you don't own (and lowering nice).
# No GTK, no spdlog: trails_core only. install.sh puts it in a root-owned
# libexec and installs the polkit policy that names it.
add_executable(trails-helper src/helper/trails_helper.cpp)
target_link_libraries(trails-helper PRIVATE trails_core)
target_link_libraries(trails_core PUBLIC Threads::Threads)
target_link_libraries(trails_selftest PRIVATE trails_core)

# ── App resources (gresource) -- the RESOURCE-PIPELINE paradigm ────────────────
# glib-compile-resources embeds resources/trails.gresource.xml into a C source;
# App::on_activate registers it and points the icon theme at the bundle so
# set_icon_name("trails-logo-symbolic") resolves out of the binary. The generated
# .c compiles as C (project enables C above) so trails_resources_get_resource()
# has C linkage, matching the extern "C" prototype in App.cpp. Adding an icon:
# drop the .svg in resources/icons/scalable/apps/, list it in the .xml, rebuild.
# (docs/resources-and-dialog-lifetime.md)
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(TRAILS_GRESOURCE_XML "${CMAKE_CURRENT_SOURCE_DIR}/resources/trails.gresource.xml")
set(TRAILS_RESOURCES_C   "${CMAKE_CURRENT_BINARY_DIR}/trails-resources.c")
file(GLOB TRAILS_RESOURCE_FILES
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/scalable/apps/*.svg"
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.css")
add_custom_command(
    OUTPUT  "${TRAILS_RESOURCES_C}"
    COMMAND "${GLIB_COMPILE_RESOURCES}"
            --target "${TRAILS_RESOURCES_C}"
            --generate-source
            --c-name trails_resources
            trails.gresource.xml
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    DEPENDS "${TRAILS_GRESOURCE_XML}" ${TRAILS_RESOURCE_FILES}
    COMMENT "Compiling gresource bundle (trails-resources.c)"
    VERBATIM)

# ── cairn -- the GTK app ───────────────────────────────────────────────────────
# The Shell is a SPLIT CLASS across TUs by category (see the index in
# include/Shell.hpp); all parts compile into the one trails::Shell.
add_executable(trails
    ${TRAILS_RESOURCES_C}
    src/main.cpp
    src/App.cpp
    # Follows the desktop's light/dark preference (GTK4 will not do it for us
    # and we do not link libadwaita). Needs no new dependency: the D-Bus and
    # GSettings calls are GIO, which gtkmm-4.0 already brings in.
    src/Appearance.cpp
    src/Shell.cpp
    src/Shell_zones.cpp
    src/Shell_bindings.cpp
    src/Shell_handlers.cpp
    src/Shell_helpers.cpp
    src/Shell_live.cpp
    # s006 -- process actions: the verbs through the journal, the dialogs they
    # ask, and the pkexec door to trails-helper.
    src/Shell_acts.cpp
    src/Shell_record.cpp
    src/ActDialogs.cpp
    src/Elevate.cpp
    src/widgets/Graph.cpp
    src/widgets/Tile.cpp
    src/widgets/Bar.cpp
    src/pages/CpuPage.cpp
    src/pages/MemoryPage.cpp
    src/pages/ProcessesPage.cpp
    src/pages/DisksPage.cpp
    src/pages/NetworkPage.cpp
    src/pages/SensorsPage.cpp
    src/pages/GpuPage.cpp
    src/AboutWindow.cpp
    src/ShortcutsDialog.cpp
    src/Registry.cpp
    src/Log.cpp
)
target_include_directories(trails PRIVATE include ${GTKMM_INCLUDE_DIRS})
target_link_libraries(trails     PRIVATE ${GTKMM_LIBRARIES} trails_core spdlog::spdlog)
target_link_directories(trails   PRIVATE ${GTKMM_LIBRARY_DIRS})
target_compile_options(trails    PRIVATE ${GTKMM_CFLAGS_OTHER})
