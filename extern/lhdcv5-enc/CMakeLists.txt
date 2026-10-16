# LHDC V5 encoder - portable C port
#
# Works both as a plain CMake project/subdirectory and as an ESP-IDF component
# (drop this directory into your project's `components/` folder).

set(LHDCV5_SOURCES
    src/lhdcv5_math.c
    src/lhdcv5_arith.c
    src/lhdcv5_bitout.c
    src/lhdcv5_fft.c
    src/lhdcv5_level.c
    src/lhdcv5_header.c
    src/lhdcv5_freq.c
    src/lhdcv5_tables.c
    src/lhdcv5_workspace.c
    src/lhdcv5_process.c
    src/lhdcv5_cirbuf.c
    src/lhdcv5_api.c)

if(ESP_PLATFORM)
  idf_component_register(
    SRCS ${LHDCV5_SOURCES}
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS src)
  # ESP32: cap at 96 kHz by default to save RAM; override as you need.
  # target_compile_definitions(${COMPONENT_LIB} PRIVATE LHDCV5_MAX_SAMPLE_RATE=96000)
  return()
endif()

cmake_minimum_required(VERSION 3.13)
project(lhdcv5_enc C)

add_library(lhdcv5_enc STATIC ${LHDCV5_SOURCES})
target_include_directories(lhdcv5_enc PUBLIC include PRIVATE src)
set_property(TARGET lhdcv5_enc PROPERTY C_STANDARD 99)

# Optional RAM cap: cmake -DLHDCV5_MAX_SAMPLE_RATE=48000 ...
if(DEFINED LHDCV5_MAX_SAMPLE_RATE)
  target_compile_definitions(lhdcv5_enc PRIVATE
      LHDCV5_MAX_SAMPLE_RATE=${LHDCV5_MAX_SAMPLE_RATE})
endif()

option(LHDCV5_BUILD_TESTS "Build test harness" OFF)
if(LHDCV5_BUILD_TESTS)
  add_executable(ref_encode_c test/ref_encode_c.c)
  target_link_libraries(ref_encode_c lhdcv5_enc)
endif()

option(LHDCV5_BUILD_EXAMPLES "Build examples" OFF)
if(LHDCV5_BUILD_EXAMPLES)
  add_executable(encode_stdin examples/encode_stdin.c)
  target_link_libraries(encode_stdin lhdcv5_enc)
endif()
