# make_dist.cmake - assemble the release package (run via `cmake -P`).
#
# Invoked by the A2DPWB_dist target (app/CMakeLists.txt). Required -D variables:
#   APP_VERSION   project version (e.g. 1.0.5)
#   APP_EXE       path of the built A2DPWB.exe
#   BUILD_CONFIG  build configuration of APP_EXE (must be Release)
#   SRC_DIR       repository root
#   WINDEPLOYQT_EXE  windeployqt.exe of the Qt kit the app was built with
#   QT_VERSION    Qt version (for SOURCE.txt)
#   CRT_DLLS      "|"-separated VC++ runtime DLLs to ship app-local (MSVC builds)
#   DIST_ROOT     output directory (build/dist)
#   GIT_EXE       git executable (may be empty / NOTFOUND)
#   ISCC_EXE      Inno Setup 6 compiler (may be empty / NOTFOUND: no installer)
#   APP_VERSION_NUMERIC  x.y.z without suffix, for the installer's file version
#
# Produces:
#   ${DIST_ROOT}/A2DPWB-${APP_VERSION}/                     (unpacked package)
#   ${DIST_ROOT}/A2DPWB-${APP_VERSION}-win64-portable.zip
#   ${DIST_ROOT}/A2DPWB-${APP_VERSION}-win64-setup.exe      (installer/A2DPWB.iss)

cmake_minimum_required(VERSION 3.16)

foreach(var APP_VERSION APP_EXE BUILD_CONFIG SRC_DIR WINDEPLOYQT_EXE QT_VERSION DIST_ROOT)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "make_dist.cmake: ${var} is not set")
    endif()
endforeach()

if(NOT BUILD_CONFIG STREQUAL "Release")
    message(FATAL_ERROR "A2DPWB_dist: release packages must be built from the "
        "Release configuration (got '${BUILD_CONFIG}'). "
        "Use: cmake --build build --target A2DPWB_dist --config Release")
endif()

set(PKG_NAME "A2DPWB-${APP_VERSION}")
set(PKG_DIR "${DIST_ROOT}/${PKG_NAME}")
set(ZIP_FILE "${DIST_ROOT}/${PKG_NAME}-win64-portable.zip")
set(SETUP_FILE "${DIST_ROOT}/${PKG_NAME}-win64-setup.exe")

file(REMOVE_RECURSE "${PKG_DIR}")
file(REMOVE "${ZIP_FILE}" "${SETUP_FILE}")

# copy_required(<src> <dest relative to PKG_DIR>)
function(copy_required src dest)
    if(NOT EXISTS "${src}")
        message(FATAL_ERROR "A2DPWB_dist: required file not found: ${src}\n"
            "Run: git submodule update --init --recursive (and re-run CMake "
            "configure so Qt is found).")
    endif()
    configure_file("${src}" "${PKG_DIR}/${dest}" COPYONLY)
endfunction()

# --- Executable and top-level documents ---
copy_required("${APP_EXE}"                         "A2DPWB.exe")

# --- Qt runtime: the DLLs and the platform plugin the app needs ---
execute_process(
    COMMAND "${WINDEPLOYQT_EXE}" --release --no-translations --no-system-d3d-compiler
            --no-opengl-sw --no-system-dxc-compiler --no-compiler-runtime
            --skip-plugin-types iconengines,generic,networkinformation,tls,styles,imageformats
            --dir "${PKG_DIR}" "${PKG_DIR}/A2DPWB.exe"
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "A2DPWB_dist: windeployqt failed")
endif()
# VC++ runtime next to the exe, so no redistributable has to be installed
if(CRT_DLLS)
    string(REPLACE "|" ";" _crt_list "${CRT_DLLS}")
    foreach(dll IN LISTS _crt_list)
        get_filename_component(_name "${dll}" NAME)
        copy_required("${dll}" "${_name}")
    endforeach()
endif()

set(EXT "${SRC_DIR}/extern")

# --- SOURCE.txt (written offer / source location) ---
set(REPO_URL "(not published: set a remote named origin)")
set(HEAD_COMMIT "unknown (git not available)")
set(SUBMODULES "  (git not available)")
set(DIRTY_NOTE "")
if(GIT_EXE AND EXISTS "${GIT_EXE}")
    execute_process(COMMAND "${GIT_EXE}" rev-parse HEAD
        WORKING_DIRECTORY "${SRC_DIR}"
        OUTPUT_VARIABLE HEAD_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "A2DPWB_dist: git rev-parse HEAD failed")
    endif()
    execute_process(COMMAND "${GIT_EXE}" remote get-url origin
        WORKING_DIRECTORY "${SRC_DIR}"
        OUTPUT_VARIABLE _origin OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _orc)
    if(_orc EQUAL 0 AND _origin)
        string(REGEX REPLACE "\.git$" "" REPO_URL "${_origin}")
    endif()
    execute_process(COMMAND "${GIT_EXE}" submodule status
        WORKING_DIRECTORY "${SRC_DIR}"
        OUTPUT_VARIABLE SUBMODULES OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "A2DPWB_dist: git submodule status failed")
    endif()
    execute_process(COMMAND "${GIT_EXE}" status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${SRC_DIR}"
        OUTPUT_VARIABLE _status OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _status STREQUAL "")
        set(DIRTY_NOTE "\nNOTE: this package was built from a working tree with uncommitted\nlocal modifications on top of the commit above.\n")
    endif()
else()
    message(WARNING "A2DPWB_dist: git not found; SOURCE.txt will not list commits")
endif()

file(WRITE "${PKG_DIR}/SOURCE.txt"
"A2DP Bridge ${APP_VERSION}

Source code
-----------
Repository: ${REPO_URL}
Commit:     ${HEAD_COMMIT}
${DIRTY_NOTE}
Pinned submodules (git submodule status: commit, path, description):
${SUBMODULES}

Qt ${QT_VERSION} (LGPL-3.0, dynamically linked: the Qt DLLs in this package
           can be replaced by your own build of the same Qt version):
           https://download.qt.io/official_releases/qt/ (source packages)

The complete corresponding source code of this program and of every
library statically linked into A2DPWB.exe - including the LGPL-2.1-or-later
libopenaptx and the Fraunhofer FDK AAC codec - is available free of charge
at the repository URL and commits listed above. It contains everything
needed to rebuild and relink the executable (e.g. with a modified version
of libopenaptx):

    git clone --recursive ${REPO_URL}.git
    git checkout ${HEAD_COMMIT}
    git submodule update --init --recursive

See README.md in the repository for build instructions.
")

# --- Zip (paths inside are relative: A2DPWB-<ver>/...) ---
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${ZIP_FILE}" --format=zip "${PKG_NAME}"
    WORKING_DIRECTORY "${DIST_ROOT}"
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "A2DPWB_dist: failed to create ${ZIP_FILE}")
endif()

message(STATUS "Release package: ${ZIP_FILE}")

# --- Installer, from the same files ---
if(NOT ISCC_EXE OR NOT EXISTS "${ISCC_EXE}")
    message(WARNING "A2DPWB_dist: Inno Setup 6 (ISCC.exe) not found; skipping the installer. "
        "Install it (winget install JRSoftware.InnoSetup) and re-run CMake configure.")
    return()
endif()
if(NOT APP_VERSION_NUMERIC)
    set(APP_VERSION_NUMERIC "${APP_VERSION}")
endif()
file(TO_NATIVE_PATH "${PKG_DIR}" _pkg_native)
file(TO_NATIVE_PATH "${DIST_ROOT}" _out_native)
execute_process(
    COMMAND "${ISCC_EXE}" /Q
        "/DAppVersion=${APP_VERSION}"
        "/DAppVersionNumeric=${APP_VERSION_NUMERIC}"
        "/DPkgDir=${_pkg_native}"
        "/DOutputDir=${_out_native}"
        "${SRC_DIR}/installer/A2DPWB.iss"
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0 OR NOT EXISTS "${SETUP_FILE}")
    message(FATAL_ERROR "A2DPWB_dist: Inno Setup failed to build ${SETUP_FILE}")
endif()
message(STATUS "Installer: ${SETUP_FILE}")
