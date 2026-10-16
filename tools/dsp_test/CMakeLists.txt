# a2dpwb_dsp_test - self-check of app/src/audio_dsp.cpp (downmix, soft clip,
# float -> integer conversion). Developer tool: not part of the release package.
add_executable(a2dpwb_dsp_test dsp_test.cpp ${CMAKE_SOURCE_DIR}/app/src/audio_dsp.cpp)
target_include_directories(a2dpwb_dsp_test PRIVATE ${CMAKE_SOURCE_DIR}/app/src)
