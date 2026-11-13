set(MODULES_DIR ${PROJECT_SOURCE_DIR}/modules)

# miniaudio (the device, the mixer graph, wav/mp3/flac decoding) ships an implementation
# translation unit of its own, so nothing has to be written out here. A consumer that already has
# the target keeps it.
#
# Three subsystems are compiled out rather than left unused. The resource manager is the one that
# matters: it is miniaudio's own file loader, and it would both spawn a job thread nobody asked for
# and give the mixer a second way to acquire a sound - one that bypasses the consumer's assets.
# Removing it makes that seam a link error rather than a rule.
if(NOT TARGET miniaudio)
    message("[VKM_AUDIO] Modules:")
    message("\t- miniaudio | Path: ${MODULES_DIR}/miniaudio (https://github.com/mackron/miniaudio)")

    add_library(miniaudio STATIC ${MODULES_DIR}/miniaudio/miniaudio.c)
    # GCC's -O3 reads two writes in the mp3 decoder's scale factors (ma_dr_mp3_L3_decode_scalefactors)
    # as past their region; the source stays as its authors ship it.
    target_compile_options(miniaudio PRIVATE $<$<C_COMPILER_ID:GNU>:-Wno-stringop-overflow>)
    # Split for export: miniaudio travels in a consumer's link interface when vkm_audio is linked
    # into a library it installs, so its include path has to resolve against an install too.
    target_include_directories(miniaudio SYSTEM PUBLIC
        $<BUILD_INTERFACE:${MODULES_DIR}/miniaudio>
        $<INSTALL_INTERFACE:include>
    )
    # PUBLIC because the header reads them as well: a source that compiled with a different set
    # would see a different ma_engine_config layout.
    target_compile_definitions(miniaudio PUBLIC
        MA_NO_ENCODING
        MA_NO_GENERATION
        MA_NO_RESOURCE_MANAGER
    )
    # The Linux backends (ALSA / PulseAudio / JACK) are dlopen'ed at runtime, so miniaudio links
    # none of them - it needs the loader and a thread library.
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()
