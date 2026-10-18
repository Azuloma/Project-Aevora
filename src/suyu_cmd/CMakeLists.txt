# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# Credits to Samantas5855 and others for this function.
function(create_resource file output filename)
    # Read hex data from file
    file(READ ${file} filedata HEX)
    # Convert hex data for C compatibility
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," filedata ${filedata})
    # Write data to output file
    set(RESOURCES_DIR "${PROJECT_BINARY_DIR}/dist" PARENT_SCOPE)
    file(WRITE "${PROJECT_BINARY_DIR}/dist/${output}" "const unsigned char ${filename}[] = {${filedata}};\nconst unsigned ${filename}_size = sizeof(${filename});\n")
endfunction()

if (SUYU_CMD OR YUZU_CMD)
    set(SUYU_CMD_SOURCES
        emu_window/emu_window_sdl2.cpp
        emu_window/emu_window_sdl2.h
        emu_window/emu_window_sdl2_gl.cpp
        emu_window/emu_window_sdl2_gl.h
        emu_window/emu_window_sdl2_null.cpp
        emu_window/emu_window_sdl2_null.h
        emu_window/emu_window_sdl2_vk.cpp
        emu_window/emu_window_sdl2_vk.h
        precompiled_headers.h
        controller_assign.h
        fullscreen_toggle.h
        native_status.cpp
        native_status.h
        overlay/build_identity.cpp
        overlay/build_identity.h
        overlay/controller_settings.h
        overlay/debug_overlay.cpp
        overlay/debug_overlay.h
        overlay/graphics_settings.h
        overlay/mods_folders.h
        overlay/overlay_actions.h
        overlay/overlay_logic.h
        overlay/overlay_stats.h
        overlay/overlay_style.cpp
        overlay/overlay_style.h
        overlay/perf_hud.h
        overlay/perf_hud_sampler.cpp
        overlay/perf_hud_sampler.h
        overlay/rebind_session.h
        sdl_config.cpp
        resolution_scale_host.h
        sdl_config.h
        suyu.cpp
        suyu.rc
        # video_core does not compile the VMA implementation; every executable does.
        ../video_core/vulkan_common/vma.cpp
    )

    add_executable(suyu-cmd ${SUYU_CMD_SOURCES})
    set(SUYU_CMD_TARGETS suyu-cmd)

    # ---------------------------------------------------------------------
    # Per-game statically recompiled variant.
    #
    # SUYU_CMD_RECOMP_DIR points at one export's exefs/ directory, which holds
    # a generated recomp_registration.c plus one subdirectory per NSO module
    # with a generated CMakeLists.txt. Each of those contributes a static
    # library (recomp_static_<module>) whose C symbols have been renamed per
    # module, so they can all live in one binary. The result is a single
    # self-contained executable: no recompiled_*.dll to ship beside it.
    #
    # This is an *additional* target - plain suyu-cmd stays game-agnostic.
    # ---------------------------------------------------------------------
    if (SUYU_CMD_RECOMP_DIR AND EXISTS "${SUYU_CMD_RECOMP_DIR}/recomp_registration.c")
        if (NOT EXISTS "${SUYU_CMD_RECOMP_DIR}/recomp_abi_v4.h")
            message(FATAL_ERROR
                "This static module registry predates ABI 4. Re-export the title before building.")
        endif()
        enable_language(C)
        # Tells the generated module projects to emit only the static library,
        # skipping their portable standalone exe (whose target name would
        # collide between modules) and their loadable shared image.
        set(RECOMP_STATIC_ONLY ON)
        set(_recomp_libs "")
        # Generated block bodies re-declare temporaries per guest block and
        # leave results unused wherever the original instruction's value is
        # dead, so suyu's warnings-as-errors fire constantly on machine-written
        # code that nobody is going to read, let alone edit. -Wshadow alone
        # fails the build on Mario Kart 8 Deluxe's main module. The MSVC branch
        # of the generated CMakeLists already silences its equivalents; do the
        # same for everyone else by dropping -Werror for these subdirectories
        # only. The host's own flags are restored once they have been added, so
        # suyu's sources keep the full set.
        get_directory_property(_recomp_host_opts COMPILE_OPTIONS)
        set(_recomp_quiet_opts "${_recomp_host_opts}")
        # Matched anywhere in the element, not anchored: these arrive wrapped
        # in a generator expression, e.g.
        # "$<$<COMPILE_LANGUAGE:C,CXX>:-Werror=shadow>", so an anchored pattern
        # matches none of them and silently leaves the build exactly as it was.
        list(FILTER _recomp_quiet_opts EXCLUDE REGEX "-Werror|/WX")
        set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_recomp_quiet_opts}")
        # file(GLOB) treats [ ] * ? in the WHOLE path as glob syntax, not just
        # the trailing "/*" - a title whose exported folder name carries its
        # title ID in brackets (e.g. "Pokemon Sword[0100ABF008968000][US][v0]",
        # produced whenever a ROM has no NACP title to name the folder from)
        # silently matches nothing, so every module looks missing even though
        # recomp_export.c/CMakeLists.txt are sitting right there on disk.
        # Escaping each special character to its bracket-literal form before
        # globbing keeps this working for any export path, not just ones that
        # happen to avoid those characters.
        # Order matters: each replacement's own output must not be re-mangled
        # by a later replacement, so "[" goes through a placeholder that
        # contains no bracket characters until every other substitution has
        # already run.
        set(_recomp_dir_escaped "${SUYU_CMD_RECOMP_DIR}")
        string(REPLACE "[" "@LBRACKET@" _recomp_dir_escaped "${_recomp_dir_escaped}")
        string(REPLACE "]" "[]]" _recomp_dir_escaped "${_recomp_dir_escaped}")
        string(REPLACE "*" "[*]" _recomp_dir_escaped "${_recomp_dir_escaped}")
        string(REPLACE "?" "[?]" _recomp_dir_escaped "${_recomp_dir_escaped}")
        string(REPLACE "@LBRACKET@" "[[]" _recomp_dir_escaped "${_recomp_dir_escaped}")
        file(GLOB _recomp_children RELATIVE "${SUYU_CMD_RECOMP_DIR}"
             "${_recomp_dir_escaped}/*")
        # Hybrid exports retain generated sources for modules that failed to
        # build. Only the modules in the registry may participate in the link.
        set(SUYU_RECOMP_DLL_MODULES "")
        if (EXISTS "${SUYU_CMD_RECOMP_DIR}/recomp_modules.cmake")
            include("${SUYU_CMD_RECOMP_DIR}/recomp_modules.cmake")
            set(_recomp_children "${SUYU_RECOMP_MODULES}")
        endif()
        # Topology v1 (a split export): some modules are shared images (DLLs)
        # beside the launcher. The exporter writes the marker together with the
        # v1 registry and the DLL variables in recomp_modules.cmake, from one
        # topology value; the two must agree, or the registry would name a
        # binding the build does not produce.
        set(_recomp_topology_v1_marker "${SUYU_CMD_RECOMP_DIR}/recomp_topology_v1.h")
        set(_recomp_topology_v1 OFF)
        if (EXISTS "${_recomp_topology_v1_marker}")
            set(_recomp_topology_v1 ON)
        endif()
        if (SUYU_RECOMP_DLL_MODULES AND NOT _recomp_topology_v1)
            message(FATAL_ERROR
                "This export binds module(s) ${SUYU_RECOMP_DLL_MODULES} as DLLs, but "
                "${_recomp_topology_v1_marker} is missing; the DLL binding needs the topology v1 "
                "registry. Re-export the title.")
        endif()
        if (_recomp_topology_v1 AND NOT SUYU_RECOMP_DLL_MODULES)
            message(FATAL_ERROR
                "${_recomp_topology_v1_marker} marks a topology v1 export, but "
                "recomp_modules.cmake binds no module as a DLL (SUYU_RECOMP_DLL_MODULES is empty). "
                "Re-export the title.")
        endif()
        if (_recomp_topology_v1)
            include("${CMAKE_CURRENT_LIST_DIR}/recomp_modules/RecompDllTarget.cmake")
            if (NOT SUYU_RECOMP_TOPOLOGY_VERSION EQUAL 1)
                message(FATAL_ERROR "recomp_modules.cmake topology version "
                                    "'${SUYU_RECOMP_TOPOLOGY_VERSION}' is not supported; expected 1")
            endif()
            foreach(_m ${SUYU_RECOMP_DLL_MODULES})
                if (NOT "${_m}" IN_LIST SUYU_RECOMP_MODULES)
                    message(FATAL_ERROR "DLL module '${_m}' is not in SUYU_RECOMP_MODULES "
                                        "'${SUYU_RECOMP_MODULES}'")
                endif()
                foreach(_item SUYU_RECOMP_DLL_TARGET_${_m} SUYU_RECOMP_DLL_FILE_${_m})
                    if (NOT ${_item})
                        message(FATAL_ERROR "recomp_modules.cmake does not define ${_item} for "
                                            "DLL module '${_m}'")
                    endif()
                endforeach()
            endforeach()
            if (NOT SUYU_RECOMP_BUNDLE_ID OR NOT SUYU_RECOMP_BUNDLE_ID_BYTES)
                message(FATAL_ERROR "recomp_modules.cmake has no bundle id for the DLL modules "
                                    "${SUYU_RECOMP_DLL_MODULES}")
            endif()
        endif()
        set(_recomp_dll_targets "")
        # SUYU_CMD_RECOMP_PREBUILT_DIR: the modules were already compiled by
        # recomp_modules/ (the Windows Build export does that with clang-cl,
        # since one CMake project has one C compiler). Link its libraries
        # instead of compiling the generated sources in this tree.
        # The shared runtime library exists only where a static module was built, so a
        # split export whose every module is DLL-bound has none to import.
        set(_recomp_static_modules "")
        foreach(_m ${_recomp_children})
            if (NOT "${_m}" IN_LIST SUYU_RECOMP_DLL_MODULES)
                list(APPEND _recomp_static_modules "${_m}")
            endif()
        endforeach()
        if (SUYU_CMD_RECOMP_PREBUILT_DIR AND (NOT _recomp_topology_v1 OR _recomp_static_modules))
            set(_recomp_runtime_lib "${SUYU_CMD_RECOMP_PREBUILT_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}recomp_runtime_shared${CMAKE_STATIC_LIBRARY_SUFFIX}")
            if (NOT EXISTS "${_recomp_runtime_lib}")
                message(FATAL_ERROR "SUYU_CMD_RECOMP_PREBUILT_DIR has no ${_recomp_runtime_lib}")
            endif()
            add_library(recomp_runtime_shared STATIC IMPORTED)
            set_target_properties(recomp_runtime_shared PROPERTIES
                IMPORTED_LOCATION "${_recomp_runtime_lib}")
        endif()
        foreach(_m ${SUYU_RECOMP_DLL_MODULES})
            if (NOT EXISTS "${SUYU_CMD_RECOMP_DIR}/${_m}/CMakeLists.txt" OR
                NOT EXISTS "${SUYU_CMD_RECOMP_DIR}/${_m}/recomp_export.c")
                message(FATAL_ERROR "DLL module '${_m}' has no generated sources in "
                                    "${SUYU_CMD_RECOMP_DIR}/${_m}")
            endif()
        endforeach()
        foreach(_m ${_recomp_children})
            if (EXISTS "${SUYU_CMD_RECOMP_DIR}/${_m}/CMakeLists.txt" AND
                EXISTS "${SUYU_CMD_RECOMP_DIR}/${_m}/recomp_export.c")
                # Each generated module carries its own frozen copy of the
                # GuestContext layout. A cache produced before TPIDRRO_EL0 was
                # split out of TPIDR_EL0 still folds the two registers into one
                # field, which silently breaks IPC (the guest emits its message
                # header outside the TLS region the kernel parses it from).
                # Refuse to build against such a cache instead of shipping a
                # binary that black-screens.
                if (EXISTS "${SUYU_CMD_RECOMP_DIR}/${_m}/recomp_runtime.h")
                    file(READ "${SUYU_CMD_RECOMP_DIR}/${_m}/recomp_runtime.h" _rt_h)
                    if (NOT _rt_h MATCHES "#define RECOMP_IMAGE_ABI ([56])")
                        message(FATAL_ERROR "Module '${_m}' predates correctness ABI 5; re-export ALL modules")
                    endif()
                    # ABI 6 extends the GuestContext that all modules share
                    # with one runtime, so a bundle must not mix ABIs.
                    set(_m_abi "${CMAKE_MATCH_1}")
                    if (DEFINED _recomp_bundle_abi AND NOT _recomp_bundle_abi STREQUAL _m_abi)
                        message(FATAL_ERROR "Module '${_m}' is ABI ${_m_abi} but others are ABI "
                                            "${_recomp_bundle_abi}; re-export ALL modules together")
                    endif()
                    set(_recomp_bundle_abi "${_m_abi}")
                    if (NOT _rt_h MATCHES "tpidrro_el0")
                        message(FATAL_ERROR
                            "Recompiled module '${_m}' in ${SUYU_CMD_RECOMP_DIR} predates the "
                            "TPIDRRO_EL0/TPIDR_EL0 split and would mis-handle IPC. "
                            "Re-export the game to regenerate its AOT cache.")
                    endif()
                endif()
                if (NOT EXISTS "${SUYU_CMD_RECOMP_DIR}/${_m}/recomp_runtime.h")
                    message(FATAL_ERROR "Generated module '${_m}' has no runtime header")
                endif()
                if (_recomp_topology_v1 AND "${_m}" IN_LIST SUYU_RECOMP_DLL_MODULES)
                    # A DLL-bound module is never linked into the launcher. The
                    # launcher loads it from its own directory; the registry names it.
                    if (NOT SUYU_CMD_RECOMP_PREBUILT_DIR)
                        # Built here as a shared image. It goes to recomp_dll/,
                        # not bin/, so the non-static suyu-cmd.exe's legacy
                        # recompiled_*.dll probe in bin/ never sees a game DLL.
                        set(RECOMP_STATIC_ONLY OFF)
                        set(RECOMP_BUILD_STATIC_LIB OFF)
                        add_subdirectory("${SUYU_CMD_RECOMP_DIR}/${_m}"
                                         "${CMAKE_CURRENT_BINARY_DIR}/recomp/${_m}")
                        set(RECOMP_STATIC_ONLY ON)
                        unset(RECOMP_BUILD_STATIC_LIB)
                        suyu_recomp_setup_dll_target("${_m}" "${CMAKE_BINARY_DIR}/recomp_dll")
                        list(APPEND _recomp_dll_targets "${SUYU_RECOMP_DLL_TARGET_${_m}}")
                    endif()
                    # With SUYU_CMD_RECOMP_PREBUILT_DIR the exporter took the DLL
                    # from the prebuild tree (recomp_modules/).
                    continue()
                endif()
                if (SUYU_CMD_RECOMP_PREBUILT_DIR)
                    set(_recomp_lib "${SUYU_CMD_RECOMP_PREBUILT_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}recomp_static_${_m}${CMAKE_STATIC_LIBRARY_SUFFIX}")
                    if (NOT EXISTS "${_recomp_lib}")
                        message(FATAL_ERROR "SUYU_CMD_RECOMP_PREBUILT_DIR has no ${_recomp_lib}")
                    endif()
                    add_library(recomp_static_${_m} STATIC IMPORTED)
                    set_target_properties(recomp_static_${_m} PROPERTIES
                        IMPORTED_LOCATION "${_recomp_lib}"
                        INTERFACE_LINK_LIBRARIES recomp_runtime_shared)
                    list(APPEND _recomp_libs "recomp_static_${_m}")
                    continue()
                endif()
                add_subdirectory("${SUYU_CMD_RECOMP_DIR}/${_m}"
                                 "${CMAKE_CURRENT_BINARY_DIR}/recomp/${_m}")
                # The generated CMakeLists renames each module's exported
                # symbols so that several modules can share one binary, but
                # its list predates these three and every module defines
                # them, so linking more than one fails on duplicates.
                # Rename them the same way until the emitter does.
                # (recomp_modules/CMakeLists.txt repeats this.)
                target_compile_definitions(recomp_static_${_m} PRIVATE
                    recomp_image_index=recomp_image_index_${_m}
                    recomp_image_index32=recomp_image_index32_${_m}
                    _recomp_index32_view=_recomp_index32_view_${_m}
                    recomp_build_index=recomp_build_index_${_m})
                list(APPEND _recomp_libs "recomp_static_${_m}")
            endif()
        endforeach()
        set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_recomp_host_opts}")
        # A split export can bind every module as a DLL, leaving no static library.
        if (_recomp_libs OR _recomp_topology_v1)
            add_executable(suyu-cmd-static ${SUYU_CMD_SOURCES}
                           "${SUYU_CMD_RECOMP_DIR}/recomp_registration.c")
            target_compile_definitions(suyu-cmd-static PRIVATE SUYU_CMD_STATIC_RECOMP=1)
            if (_recomp_topology_v1)
                # recomp_registration.c is a registry v1 (carries ABI/features per
                # entry), so the v4 handshake definitions below do not apply.
                target_compile_definitions(suyu-cmd-static PRIVATE SUYU_RECOMP_REGISTRY_V1=1)
                # The launcher does not link the DLLs, but they must be built
                # before the export collects them.
                if (_recomp_dll_targets)
                    add_dependencies(suyu-cmd-static ${_recomp_dll_targets})
                endif()
            endif()
            if (NOT SUYU_RECOMP_HYBRID)
                target_compile_definitions(suyu-cmd-static PRIVATE
                                           SUYU_CMD_STATIC_RECOMP_STRICT=1)
            else()
                target_compile_definitions(suyu-cmd-static PRIVATE
                                           SUYU_CMD_STATIC_RECOMP_HYBRID=1)
            endif()
            if (NOT _recomp_topology_v1 AND EXISTS "${SUYU_CMD_RECOMP_DIR}/recomp_guard_v2.h")
                target_compile_definitions(suyu-cmd-static PRIVATE SUYU_RECOMP_GUARD_V2=1)
            endif()
            if (NOT _recomp_topology_v1 AND _recomp_bundle_abi STREQUAL "6")
                if (NOT EXISTS "${SUYU_CMD_RECOMP_DIR}/recomp_fastmem_v1.h")
                    message(FATAL_ERROR "ABI 6 modules need a registry with the fastmem "
                                        "handshake; re-export the title with this build")
                endif()
                target_compile_definitions(suyu-cmd-static PRIVATE SUYU_RECOMP_FASTMEM_V1=1)
                # The registry's feature report, which suyu.cpp checks for bits
                # this host does not implement before any other handshake.
                if (NOT EXISTS "${SUYU_CMD_RECOMP_DIR}/recomp_features_v1.h")
                    message(FATAL_ERROR "ABI 6 modules need a registry with the feature "
                                        "report; re-export the title with this build")
                endif()
                target_compile_definitions(suyu-cmd-static PRIVATE SUYU_RECOMP_FEATURES_V1=1)
                # Generation code guard (GG1) registrations, when the export has them.
                if (EXISTS "${SUYU_CMD_RECOMP_DIR}/recomp_guard_gen_v1.h")
                    target_compile_definitions(suyu-cmd-static PRIVATE SUYU_RECOMP_GUARD_GEN_V1=1)
                endif()
                # FPX1 registries also export suyu_recomp_static_fpx_v1.
                if (EXISTS "${SUYU_CMD_RECOMP_DIR}/recomp_fpx_v1.h")
                    target_compile_definitions(suyu-cmd-static PRIVATE SUYU_RECOMP_FPX_V1=1)
                endif()
            endif()
            if (_recomp_libs)
                target_link_libraries(suyu-cmd-static PRIVATE ${_recomp_libs})
            endif()
            list(APPEND SUYU_CMD_TARGETS suyu-cmd-static)
        else()
            message(WARNING "SUYU_CMD_RECOMP_DIR set but no recompiled modules found in it")
        endif()
    endif()

    create_resource("../../dist/suyu.bmp" "suyu_cmd/suyu_icon.h" "suyu_icon")

    # The overlay fonts are compiled once into a library shared by every suyu-cmd target.
    set(AEVORA_OVERLAY_FONTS
        "aevora_overlay_font_latin|Roboto/Roboto-VariableFont.ttf"
        "aevora_overlay_font_japanese|BIZUDPGothic/BIZUDPGothic-Regular.ttf")
    set(AEVORA_OVERLAY_FONT_SOURCES)
    foreach(_font ${AEVORA_OVERLAY_FONTS})
        string(REPLACE "|" ";" _font_parts "${_font}")
        list(GET _font_parts 0 _font_symbol)
        list(GET _font_parts 1 _font_file)
        set(_font_ttf "${PROJECT_SOURCE_DIR}/dist/fonts/${_font_file}")
        set(_font_cpp "${PROJECT_BINARY_DIR}/dist/suyu_cmd/${_font_symbol}_data.cpp")
        add_custom_command(
            OUTPUT "${_font_cpp}"
            COMMAND ${CMAKE_COMMAND} -DINPUT=${_font_ttf} -DOUTPUT=${_font_cpp}
                    -DSYMBOL=${_font_symbol} -P "${PROJECT_SOURCE_DIR}/CMakeModules/EmbedBinary.cmake"
            DEPENDS "${_font_ttf}" "${PROJECT_SOURCE_DIR}/CMakeModules/EmbedBinary.cmake"
            COMMENT "Embedding ${_font_file}" VERBATIM)
        list(APPEND AEVORA_OVERLAY_FONT_SOURCES "${_font_cpp}")
    endforeach()
    add_library(aevora_overlay_font STATIC ${AEVORA_OVERLAY_FONT_SOURCES})

    include(ExportBuildKit)
    suyu_export_build_kit_add_probes()

    foreach(_t ${SUYU_CMD_TARGETS})
        if (USE_DISCORD_PRESENCE)
            target_link_libraries(${_t} PRIVATE DiscordRPC::discord-rpc)
            target_compile_definitions(${_t} PRIVATE USE_DISCORD_PRESENCE)
        endif()
        if (APPLE)
            target_sources(${_t} PRIVATE
                emu_window/emu_window_sdl2_mtl.cpp
                emu_window/emu_window_sdl2_mtl.h
            )
        endif()

        target_link_libraries(${_t} PRIVATE common core input_common frontend_common)
        target_link_libraries(${_t} PRIVATE glad)
        if (MSVC)
            target_link_libraries(${_t} PRIVATE getopt)
        endif()
        target_link_libraries(${_t} PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)

        target_include_directories(${_t} PRIVATE ${RESOURCES_DIR})

        target_link_libraries(${_t} PRIVATE SDL3::SDL3 Vulkan::Headers VulkanMemoryAllocator Aevora::imgui aevora_overlay_font)
        # FFmpeg headers are not transitively exposed by core; add them explicitly
        target_include_directories(${_t} PRIVATE ${FFmpeg_INCLUDE_DIR})

        if(UNIX)
            if (APPLE)
                target_link_libraries(${_t} PRIVATE metal-cpp)  # Metal support for macOS
            endif()
        endif()

        if(WIN32)
            # compile as a win32 gui application instead of a console application
            if(MSVC)
                set_target_properties(${_t} PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
            elseif(MINGW)
                set_target_properties(${_t} PROPERTIES LINK_FLAGS_RELEASE "-Wl,--subsystem,windows")
            endif()
            target_link_libraries(${_t} PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)  # Fixed typo: yuzu-cmd -> suyu-cmd
            target_link_libraries(${_t} PRIVATE pdh)  # Performance HUD GPU counter
        endif()

        if (MSVC AND SUYU_USE_BUNDLED_SDL3)
            include(CopySuyuSDLDeps)
            copy_suyu_SDL_deps(${_t})
        endif()

        if (SUYU_USE_PRECOMPILED_HEADERS)
            target_precompile_headers(${_t} PRIVATE precompiled_headers.h)
        endif()

        create_target_directory_groups(${_t})
    endforeach()

    suyu_export_build_kit_add_package()

    if(UNIX AND NOT APPLE)
        install(TARGETS suyu-cmd)
    endif()
endif()
