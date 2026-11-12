# vkm_audio's own tests: TEST("what works") in tests/<topic>.cpp (harness/check.h). Each runs in a
# process of its own and is a ctest of its own, named "vkm_audio: <what works>" and labeled vkm_audio,
# so `ctest -L vkm_audio` runs the suite in the tree of a consumer that calls enable_testing() too, with
# -C <config> under a multi-config generator. It links the library alone and needs no sound device:
# every mix is rendered offline into a buffer. Beside them, checks of the library as a consumer builds
# it: its includes, its CMake, its debug flags.

file(GLOB VKM_AUDIO_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(vkm_audio_tests ${CMAKE_CURRENT_SOURCE_DIR}/harness/main.cpp ${VKM_AUDIO_TEST_SOURCES})
target_link_libraries(vkm_audio_tests PRIVATE ${PROJECT_NAME})
target_include_directories(vkm_audio_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vkm_audio_tests PRIVATE VKM_TEST_SUITE="vkm_audio" VKM_TEST_LOG)
target_compile_options(vkm_audio_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:${_vkm_audio_warnings}>
    $<$<CXX_COMPILER_ID:Clang>:-Werror=documentation>
)

# Every test the binary lists becomes a ctest when ctest runs: a test added is a test run, with no
# list to keep.
include(${CMAKE_CURRENT_SOURCE_DIR}/harness/discover.cmake)
vkm_discover_tests(vkm_audio_tests vkm_audio)

# The library as a consumer builds it.
add_test(NAME "vkm_audio: every include is its own, the standard library's, glm's or vkmLog's, and miniaudio's in device.cpp and decode.cpp alone"
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}/src -P ${CMAKE_CURRENT_SOURCE_DIR}/includes.cmake
)
add_test(NAME "vkm_audio: a consumer adds it after glm and vkm_log, and without either it stops and says which"
    COMMAND ${CMAKE_COMMAND} -DLIBRARY_DIR=${PROJECT_SOURCE_DIR} -DREQUIRES=glm|vkm_log
        -DOUT=${CMAKE_CURRENT_BINARY_DIR}/consumer -DGENERATOR=${CMAKE_GENERATOR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/harness/consumer.cmake
)
add_test(NAME "vkm_audio: it links a miniaudio the consumer added first, and otherwise adds its own with what it does not use compiled out"
    COMMAND ${CMAKE_COMMAND} -DLIBRARY_DIR=${PROJECT_SOURCE_DIR} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/miniaudio
        -DGENERATOR=${CMAKE_GENERATOR} -P ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio.cmake
)
set_tests_properties(
    "vkm_audio: every include is its own, the standard library's, glm's or vkmLog's, and miniaudio's in device.cpp and decode.cpp alone"
    "vkm_audio: a consumer adds it after glm and vkm_log, and without either it stops and says which"
    "vkm_audio: it links a miniaudio the consumer added first, and otherwise adds its own with what it does not use compiled out"
    PROPERTIES LABELS vkm_audio
)
if(CMAKE_NM)
    # The library's compile commands, which the check reads, whether or not the consumer exports its own.
    set_property(TARGET ${PROJECT_NAME} PROPERTY EXPORT_COMPILE_COMMANDS ON)
    add_test(NAME "vkm_audio: its asserts follow VKM_AUDIO_ASSERTS whatever the build type"
        COMMAND ${CMAKE_COMMAND} -DLIBRARY=${PROJECT_NAME} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}/src
            -DARCHIVE=$<TARGET_FILE:${PROJECT_NAME}> -DNM=${CMAKE_NM} -DASSERTS=${VKM_AUDIO_ASSERTS}
            -DPROFILER=OFF -DCONFIG=$<CONFIG> -DCOMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json
            -P ${CMAKE_CURRENT_SOURCE_DIR}/harness/flags.cmake
    )
    set_tests_properties("vkm_audio: its asserts follow VKM_AUDIO_ASSERTS whatever the build type"
        PROPERTIES LABELS vkm_audio)
endif()
