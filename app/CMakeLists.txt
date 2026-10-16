# A2DP Windows Bridge (A2DPWB)
# Captures system audio via WASAPI, encodes with LDAC/aptX HD/aptX LL/aptX/SBC/AAC/LHDC V5,
# and streams over Bluetooth via BTstack (WinUSB).
# GUI frontend via Qt 6 Widgets.

# --- Qt 6 (set CMAKE_PREFIX_PATH to the Qt kit, e.g. D:/Qt/6.8.3/msvc2022_64) ---
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# --- Main application ---
add_executable(A2DPWB WIN32
    src/main.cpp
    src/wasapi_capture.cpp
    src/ldac_encoder.cpp
    src/aptxhd_encoder.cpp
    src/aptxll_encoder.cpp
    src/aptx_encoder.cpp
    src/a2dp_sbc_encoder.cpp
    src/aac_encoder.cpp
    src/lhdcv5_encoder.cpp
    src/bt_device.cpp
    src/bt_adapter_enum.cpp
    src/btstack_transport.cpp
    src/hci_capture.cpp
    src/remote_sink_client.cpp
    src/test_tone.cpp
    src/afh.cpp
    src/btstack_uart_tcp_windows.c
    src/btstack_link_key_db_file.c
    src/config_path.cpp
    src/a2dp_service.cpp
    src/audio_dsp.cpp
    src/volume_sync.cpp
    src/ui/backend.cpp
    src/ui/theme.cpp
    src/ui/ui_app.cpp
    src/ui/main_window.cpp
    src/ui/profile_dialog.cpp
    src/ui/settings_dialog.cpp
    src/ui/link_quality_dialog.cpp
    src/ui/receiver_dialog.cpp
    src/ui/debug_console_dialog.cpp
    src/ui/debug_log_model.cpp
    src/ui/about_dialog.cpp
    src/ui/firmware_dialog.cpp
    src/ui/ui.qrc
    src/app_settings.cpp
    src/audio_device_enum.cpp
    src/app_audio.cpp
    src/localization.cpp
    src/profile_manager.cpp
    src/system_integration.cpp
    src/zadig_helper.cpp
    resources/app.rc
)

# Ninja does not scan .rc ICON/manifest references: rebuild the resources when they change
set_source_files_properties(resources/app.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico;${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest")

# Fixed (version-less) name: the HKCU Run autostart entry stores the exe path,
# so a name that changes per release would break autostart after every update.
set_target_properties(A2DPWB PROPERTIES OUTPUT_NAME "A2DPWB")

target_compile_definitions(A2DPWB PRIVATE _CRT_SECURE_NO_WARNINGS) # the code uses bounded strncpy/freopen on purpose

target_include_directories(A2DPWB PRIVATE
    ${CMAKE_SOURCE_DIR}/extern/libldac/inc
    ${CMAKE_SOURCE_DIR}/extern/libopenaptx
    ${CMAKE_SOURCE_DIR}/extern/json/include
)

# libldac (LDAC codec)
if(TARGET ldac)
    target_link_libraries(A2DPWB PRIVATE ldac)
endif()

# libopenaptx (aptX / aptX HD codec)
if(TARGET openaptx)
    target_link_libraries(A2DPWB PRIVATE openaptx)
endif()

# LHDC V5 encoder (experimental)
if(TARGET lhdcv5_enc)
    target_link_libraries(A2DPWB PRIVATE lhdcv5_enc)
    target_compile_definitions(A2DPWB PRIVATE LHDCV5_ENCODER_AVAILABLE=1)
endif()

# BTstack (user-mode Bluetooth stack via WinUSB)
if(TARGET btstack)
    target_link_libraries(A2DPWB PRIVATE btstack)
endif()

# SBC encoder (BTstack's Bluedroid)
if(TARGET sbc_codec)
    target_link_libraries(A2DPWB PRIVATE sbc_codec)
endif()

# fdk-aac (AAC-LC encoder)
if(TARGET fdk-aac)
    target_link_libraries(A2DPWB PRIVATE fdk-aac)
    target_compile_definitions(A2DPWB PRIVATE AAC_ENCODER_AVAILABLE=1)
    target_include_directories(A2DPWB PRIVATE
        ${CMAKE_SOURCE_DIR}/extern/fdk-aac/libAACenc/include
        ${CMAKE_SOURCE_DIR}/extern/fdk-aac/libSYS/include
    )
endif()

# Qt (Widgets pulls in Gui and Core)
target_link_libraries(A2DPWB PRIVATE Qt6::Widgets)
target_include_directories(A2DPWB PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/ui)

# Windows system libraries
target_link_libraries(A2DPWB PRIVATE
    ole32          # COM (CoInitializeEx, CoCreateInstance)
    mmdevapi       # ActivateAudioInterfaceAsync (Application capture)
    uuid           # COM interface UUIDs (__uuidof)
    avrt           # Multimedia scheduling (AvSetMmThreadCharacteristics)
    bthprops       # Bluetooth device enumeration (BluetoothFindFirstDevice)
    ws2_32         # Winsock (Bluetooth address types)
    propsys        # Property store (PKEY_Device_FriendlyName)
    version        # GetFileVersionInfo (friendly app names)
    setupapi       # SetupDi (Zadig USB enumeration)
    shell32        # Shell_NotifyIcon (system tray)
    shcore         # SetProcessDpiAwareness (high-DPI)
    winmm          # timeBeginPeriod/timeEndPeriod (timer resolution)
    winhttp        # Update check (GitHub Releases API)
    iphlpapi       # GetAdaptersAddresses (finding the measuring receiver)
    wlanapi        # Wi-Fi survey for AFH host channel classification
    powrprof       # PowerRegisterSuspendResumeNotification (restart Bluetooth after sleep)
)

# Application version
target_compile_definitions(A2DPWB PRIVATE
    APP_VERSION="${A2DPWB_VERSION}"
)

# LDAC ABR (Adaptive Bit Rate) support - available when libldac includes abr/
if(EXISTS ${CMAKE_SOURCE_DIR}/extern/libldac/abr/src/ldacBT_abr.c)
    target_compile_definitions(A2DPWB PRIVATE LDAC_ABR_AVAILABLE=1)
    target_include_directories(A2DPWB PRIVATE
        ${CMAKE_SOURCE_DIR}/extern/libldac/abr/inc
    )
endif()

# Compile definitions
target_compile_definitions(A2DPWB PRIVATE
    _WIN32_WINNT=0x0A00   # Windows 10+
    NOMINMAX               # Avoid min/max macro conflicts
    WIN32_LEAN_AND_MEAN    # Reduce Windows header bloat
)

if(MSVC)
    target_compile_options(A2DPWB PRIVATE /W3 /utf-8)
    # Use main() entry point with WIN32 subsystem (no console window)
    target_link_options(A2DPWB PRIVATE /ENTRY:mainCRTStartup)
    # Manifest is embedded via app.rc; prevent linker from generating a duplicate
    target_link_options(A2DPWB PRIVATE /MANIFEST:NO)
else()
    target_compile_options(A2DPWB PRIVATE -Wall -Wextra)
endif()

# Embed language files into the executable at configure time
file(GLOB LANG_FILES "${CMAKE_CURRENT_SOURCE_DIR}/lang/*.json")
# Re-run the configure step when a language file changes, so the embedded
# strings cannot go stale in an incremental build
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LANG_FILES})
set(EMBEDDED_LANG_ENTRIES "")
foreach(LANG_FILE ${LANG_FILES})
    get_filename_component(LANG_CODE ${LANG_FILE} NAME_WE)
    file(READ ${LANG_FILE} LANG_CONTENT)
    # MSVC limits one string literal to ~16 KB (C2026), so emit adjacent raw
    # literals of at most 8000 bytes, split after a newline so no UTF-8
    # sequence is cut (the concatenated total may reach 64 KB)
    set(LANG_LITERALS "")
    string(LENGTH "${LANG_CONTENT}" LANG_LEN)
    while(LANG_LEN GREATER 8000)
        string(SUBSTRING "${LANG_CONTENT}" 0 8000 LANG_HEAD)
        string(FIND "${LANG_HEAD}" "\n" LANG_CUT REVERSE)
        if(LANG_CUT LESS 0)
            message(FATAL_ERROR "${LANG_FILE}: a line is longer than 8000 bytes")
        endif()
        math(EXPR LANG_CUT "${LANG_CUT} + 1")
        string(SUBSTRING "${LANG_CONTENT}" 0 ${LANG_CUT} LANG_CHUNK)
        string(SUBSTRING "${LANG_CONTENT}" ${LANG_CUT} -1 LANG_CONTENT)
        string(APPEND LANG_LITERALS "R\"LANG_JSON(${LANG_CHUNK})LANG_JSON\"\n            ")
        string(LENGTH "${LANG_CONTENT}" LANG_LEN)
    endwhile()
    string(APPEND LANG_LITERALS "R\"LANG_JSON(${LANG_CONTENT})LANG_JSON\"")
    string(APPEND EMBEDDED_LANG_ENTRIES "        {\"${LANG_CODE}\", ${LANG_LITERALS}},\n")
endforeach()
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/embedded_langs.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_langs.h
    @ONLY
)
target_include_directories(A2DPWB PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

# --- Release packages (build/dist/A2DPWB-<ver>-win64-portable.zip and -setup.exe) ---
# cmake --build build --target A2DPWB_dist --config Release
# Assembles A2DPWB.exe, LICENSE(-APACHE/-MIT), THIRD_PARTY_LICENSES.md, license texts copied
# from the pinned sources (licenses/) and SOURCE.txt (repo/submodule commits), zips them,
# and builds the installer (installer/A2DPWB.iss) from the same files when Inno Setup 6
# is installed.
# VC++ runtime DLLs shipped next to the exe by the dist target (no redistributable needed)
set(CRT_DLL_LIST "")
if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
    include(InstallRequiredSystemLibraries)
    string(REPLACE ";" "|" CRT_DLL_LIST "${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}")
endif()
find_package(Git QUIET)
find_program(A2DPWB_ISCC ISCC
    HINTS "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6"
          "$ENV{ProgramFiles\(x86\)}/Inno Setup 6"
          "$ENV{ProgramFiles}/Inno Setup 6")
set(A2DPWB_DIST_DIR ${CMAKE_BINARY_DIR}/dist)
add_custom_target(A2DPWB_dist
    COMMAND ${CMAKE_COMMAND}
        -DAPP_VERSION=${A2DPWB_VERSION}
        -DAPP_EXE=$<TARGET_FILE:A2DPWB>
        -DBUILD_CONFIG=$<CONFIG>
        -DSRC_DIR=${CMAKE_SOURCE_DIR}
        -DWINDEPLOYQT_EXE=$<TARGET_FILE:Qt6::windeployqt>
        -DQT_VERSION=${Qt6_VERSION}
        "-DCRT_DLLS=${CRT_DLL_LIST}"
        -DDIST_ROOT=${A2DPWB_DIST_DIR}
        -DGIT_EXE=${GIT_EXECUTABLE}
        -DISCC_EXE=${A2DPWB_ISCC}
        -DAPP_VERSION_NUMERIC=${PROJECT_VERSION}
        -P ${CMAKE_SOURCE_DIR}/cmake/make_dist.cmake
    COMMENT "Packaging A2DPWB ${A2DPWB_VERSION} into ${A2DPWB_DIST_DIR}"
    VERBATIM
)
add_dependencies(A2DPWB_dist A2DPWB)
