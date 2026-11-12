# A consumer that has added miniaudio itself, or not (VKM_CONSUMER_MINIAUDIO): with it, the library
# adds none of its own and links that one; without, it adds its own from modules/miniaudio, with the
# definitions that compile out what it does not use. Stand-ins for glm and vkm_log.
cmake_minimum_required(VERSION 3.25)
project(vkm_audio_consumer LANGUAGES C CXX)

add_library(glm INTERFACE)
add_library(vkm_log INTERFACE)
if(VKM_CONSUMER_MINIAUDIO)
    add_library(miniaudio INTERFACE)
endif()

add_subdirectory(${VKM_LIBRARY_DIR} library)

get_target_property(_links vkm_audio LINK_LIBRARIES)
if(NOT "miniaudio" IN_LIST _links)
    message(FATAL_ERROR "vkm_audio does not link miniaudio: ${_links}")
endif()

get_target_property(_type miniaudio TYPE)
if(VKM_CONSUMER_MINIAUDIO)
    if(NOT _type STREQUAL "INTERFACE_LIBRARY")
        message(FATAL_ERROR "The library replaced the consumer's miniaudio with a ${_type} of its own")
    endif()
    return()
endif()

get_target_property(_sources miniaudio SOURCES)
string(FIND "${_sources}" "${VKM_LIBRARY_DIR}/modules/miniaudio/miniaudio.c" _at)
if(_at EQUAL -1)
    message(FATAL_ERROR "The library's own miniaudio is not built from modules/miniaudio: ${_sources}")
endif()
get_target_property(_definitions miniaudio INTERFACE_COMPILE_DEFINITIONS)
foreach(_definition MA_NO_ENCODING MA_NO_GENERATION MA_NO_RESOURCE_MANAGER)
    if(NOT _definition IN_LIST _definitions)
        message(FATAL_ERROR "The library's own miniaudio lacks ${_definition}: ${_definitions}")
    endif()
endforeach()
