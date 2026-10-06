# SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# Verification and setup of the shared image (DLL) target of a DLL-bound module.
# Included by recomp_modules/CMakeLists.txt (clang-cl prebuild) and by
# ../CMakeLists.txt (in-tree launcher build), so both check the generated module
# CMake against the topology the same way.
#
# The topology variables come from the export's recomp_modules.cmake, which the
# exporter writes from one topology value (core/recompiler/recomp_topology.h):
#   SUYU_RECOMP_TOPOLOGY_VERSION, SUYU_RECOMP_BUNDLE_ID_BYTES,
#   SUYU_RECOMP_DLL_TARGET_<module>, SUYU_RECOMP_DLL_FILE_<module>

# suyu_recomp_setup_dll_target(<module> <output_directory>)
#
# Call after add_subdirectory() of the generated module. Fails, naming the
# module and item, when the generated target does not match the topology.
function(suyu_recomp_setup_dll_target module output_directory)
    set(_dll_target "${SUYU_RECOMP_DLL_TARGET_${module}}")
    if (NOT TARGET ${_dll_target})
        message(FATAL_ERROR "Generated module '${module}' does not define the shared image "
                            "target '${_dll_target}' that the topology expects")
    endif()
    # The file name the topology publishes must be the one this target
    # actually produces, or a later registry/package consumer looks for a
    # file that does not exist.
    get_target_property(_dll_type ${_dll_target} TYPE)
    if (NOT _dll_type STREQUAL "SHARED_LIBRARY")
        message(FATAL_ERROR "Target '${_dll_target}' of module '${module}' is ${_dll_type}, "
                            "not a shared image")
    endif()
    get_target_property(_dll_stem ${_dll_target} OUTPUT_NAME)
    if (NOT _dll_stem)
        set(_dll_stem "${_dll_target}")
    endif()
    get_target_property(_dll_prefix ${_dll_target} PREFIX)
    if (_dll_prefix MATCHES "NOTFOUND$")
        set(_dll_prefix "${CMAKE_SHARED_LIBRARY_PREFIX}")
    endif()
    get_target_property(_dll_suffix ${_dll_target} SUFFIX)
    if (_dll_suffix MATCHES "NOTFOUND$")
        set(_dll_suffix "${CMAKE_SHARED_LIBRARY_SUFFIX}")
    endif()
    set(_dll_actual_file "${_dll_prefix}${_dll_stem}${_dll_suffix}")
    if (NOT "${_dll_actual_file}" STREQUAL "${SUYU_RECOMP_DLL_FILE_${module}}")
        message(FATAL_ERROR "Module '${module}': the topology expects the file "
                            "'${SUYU_RECOMP_DLL_FILE_${module}}' but target '${_dll_target}' "
                            "produces '${_dll_actual_file}'")
    endif()
    # The raw digest as a byte list; the exported function returns it.
    target_compile_definitions(${_dll_target} PRIVATE
        RECOMP_IDENTITY_NAME="${module}"
        RECOMP_IDENTITY_BUNDLE_BYTES=${SUYU_RECOMP_BUNDLE_ID_BYTES})
    set_target_properties(${_dll_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${output_directory}"
        LIBRARY_OUTPUT_DIRECTORY "${output_directory}")
endfunction()
