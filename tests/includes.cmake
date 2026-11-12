# Every #include under src/vkm_audio is the library's own, the standard library's, glm's or vkmLog's;
# miniaudio's header is device.cpp's and decode.cpp's alone, so nothing a consumer includes reaches
# it (cmake -P, -DSOURCE_DIR=<src>).

set(_miniaudio_files "device.cpp|decode.cpp")

file(GLOB_RECURSE _files "${SOURCE_DIR}/vkm_audio/*.h" "${SOURCE_DIR}/vkm_audio/*.cpp")
set(_problems "")
foreach(_file IN LISTS _files)
    get_filename_component(_name "${_file}" NAME)
    file(STRINGS "${_file}" _includes REGEX "^[ \t]*#[ \t]*include")
    foreach(_line IN LISTS _includes)
        string(REGEX MATCH "[<\"]([^>\"]+)[>\"]" _ "${_line}")
        set(_header "${CMAKE_MATCH_1}")
        if(_header MATCHES "^vkm_audio/" AND EXISTS "${SOURCE_DIR}/${_header}")
        elseif(_line MATCHES "<[a-z_]+>")
        elseif(_header MATCHES "^(glm|vkm_log)/")
        elseif(_header STREQUAL "miniaudio.h" AND _name MATCHES "^(${_miniaudio_files})$")
        else()
            string(APPEND _problems "  ${_file}: ${_line}\n")
        endif()
    endforeach()
endforeach()

if(_problems)
    message(FATAL_ERROR "Includes the library may not have:\n${_problems}")
endif()
