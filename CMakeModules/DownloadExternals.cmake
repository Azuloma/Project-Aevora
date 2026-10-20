# SPDX-FileCopyrightText: 2017 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

function(download_bundled_external remote_path lib_name prefix_var)

set(package_base_url "https://git.suyu.dev/suyu/")
set(package_repo "no_platform")
set(package_extension "no_platform")
set(package_head "?ref_type=heads")
if (WIN32)
    set(package_repo "ext-windows-bin/raw/branch/master/")
    set(package_extension ".7z")
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(package_repo "ext-linux-bin/raw/branch/main/")
    set(package_extension ".tar.xz")
else()
    message(FATAL_ERROR "No package available for this platform")
endif()
set(package_url "${package_base_url}${package_repo}")

set(prefix "${CMAKE_BINARY_DIR}/externals/${lib_name}")
if (NOT EXISTS "${prefix}")
    message(STATUS "Downloading binaries for ${lib_name}...")
    set(_archive_path "${CMAKE_BINARY_DIR}/externals/${lib_name}${package_extension}")
    file(DOWNLOAD
        ${package_url}${remote_path}${lib_name}${package_extension}${package_head}
        "${_archive_path}" SHOW_PROGRESS)

    # Verify the downloaded archive is non-empty before attempting extraction
    if (EXISTS "${_archive_path}")
        file(SIZE "${_archive_path}" _archive_size)
    else()
        set(_archive_size 0)
    endif()

    if (_archive_size EQUAL 0)
        message(WARNING "Downloaded archive '${_archive_path}' is empty. Removing and skipping bundled external '${lib_name}'.")
        file(REMOVE "${_archive_path}")
    else()
        if (package_extension STREQUAL ".7z")
            # Prefer using 7z for .7z archives on Windows; fall back to tar with a warning
            find_program(SEVENZA_EXECUTABLE NAMES 7z 7z.exe)
            if (SEVENZA_EXECUTABLE)
                execute_process(COMMAND ${SEVENZA_EXECUTABLE} x -y "${_archive_path}"
                    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/externals")
            else()
                message(WARNING "7z not found; attempting to extract ${lib_name}${package_extension} with cmake -E tar. This may fail on .7z archives.")
                execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${_archive_path}"
                    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/externals")
            endif()
        else()
            execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${_archive_path}"
                WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/externals")
        endif()
    endif()
endif()
if(EXISTS "${prefix}")
    message(STATUS "Using bundled binaries at ${prefix}")
    set(${prefix_var} "${prefix}" PARENT_SCOPE)
else()
    message(STATUS "No bundled binaries available for ${lib_name}; falling back to system/vcpkg-installed package")
    set(${prefix_var} "" PARENT_SCOPE)
endif()
endfunction()
