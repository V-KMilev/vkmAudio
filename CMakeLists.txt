cmake_minimum_required(VERSION 3.25.0)

set(PROJECT_NAME vkm_audio)
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 0)
set(PROJECT_VERSION_PATCH 0)

project(
  ${PROJECT_NAME}
  VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message("[VKM_AUDIO] Building vkm_audio v${PROJECT_VERSION}")

# glm and vkm_log are the consuming project's to provide: glm crosses this library's API, so a
# copy in here would be a second one across the boundary. Fail with the reason rather than with
# whatever error a missing target produces three files later.
foreach(_vkm_audio_required glm vkm_log)
    if(NOT TARGET ${_vkm_audio_required})
        message(FATAL_ERROR
            "vkm_audio requires the target '${_vkm_audio_required}', which the consuming project "
            "must add before add_subdirectory(vkmAudio); see README.md.")
    endif()
endforeach()

# Debug features follow flags, each off unless the consumer turns it on.
option(VKM_AUDIO_ASSERTS "Keep the library's VKM_ASSERTs on whatever the build type" OFF)

# miniaudio, which is this library's alone.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/modules)

# Enumerated as one sweep rather than as a list: a forgotten entry in a list is a missing symbol
# somewhere else.
file(GLOB_RECURSE VKM_AUDIO_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

# Static and position-independent: a consumer links it whole into a shared library of its own, so
# it ships no binary of its own.
add_library(${PROJECT_NAME} STATIC ${VKM_AUDIO_SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The root above vkm_audio/, so an include names the library: "vkm_audio/device.h".
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    glm
    vkm_log
)

# Public: its headers are C++17 (string_view), so whatever includes them is too.
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

# Private: only device.cpp and decode.cpp include miniaudio's header, and its definitions reach the
# library's own sources alone. A consumer links the archive and nothing else of it.
target_link_libraries(${PROJECT_NAME} PRIVATE
    miniaudio
)

# Its warning set. Clang reads the doc blocks: a @param naming no parameter is an error.
set(_vkm_audio_warnings
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wshadow
    -Wunused-variable
    -Wunused-value
    -Wformat=2
    -Wnon-virtual-dtor
    -Woverloaded-virtual
)
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:${_vkm_audio_warnings}>
    $<$<CXX_COMPILER_ID:Clang>:-Werror=documentation>
)

# No fused multiply-add unless the source writes one, so a gain or a ray rounds as written, whichever
# compiler builds it. Private: a mix is heard, not replayed, so a consumer need not round the headers'
# inline math alike.
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-ffp-contract=off>
)

# VKM_ASSERT keys on NDEBUG, which a RelWithDebInfo or Release build defines: on, the library's own
# sources compile without it.
if(VKM_AUDIO_ASSERTS)
    target_compile_options(${PROJECT_NAME} PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)
endif()

# Its log lines say whose they are.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VKM_LOG_SUFFIX="VKM-AUDIO"
)

# The suite (tests/), built when the consumer asks for it.
option(VKM_AUDIO_BUILD_TESTS "Build the vkm_audio test suite" OFF)
if(VKM_AUDIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
