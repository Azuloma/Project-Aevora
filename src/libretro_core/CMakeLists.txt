# SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(suyu_libretro SHARED
    libretro.h
    retro_core.cpp
    retro_emu_window.cpp
    retro_emu_window.h
    # video_core does not compile the VMA implementation; every executable does.
    ../video_core/vulkan_common/vma.cpp
)

target_link_libraries(suyu_libretro PRIVATE common core input_common frontend_common audio_core network)
target_link_libraries(suyu_libretro PRIVATE Vulkan::Headers GPUOpen::VulkanMemoryAllocator)
target_link_libraries(suyu_libretro PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)


set_target_properties(suyu_libretro PROPERTIES
    PREFIX ""
    OUTPUT_NAME "suyu_libretro"
)

create_target_directory_groups(suyu_libretro)

add_executable(libretro_tas_frame_smoke EXCLUDE_FROM_ALL
    tas_frame_smoke.cpp retro_emu_window.cpp
    ../video_core/vulkan_common/vma.cpp)
target_link_libraries(libretro_tas_frame_smoke PRIVATE
    common core input_common frontend_common audio_core network
    Vulkan::Headers GPUOpen::VulkanMemoryAllocator
    ${PLATFORM_LIBRARIES} Threads::Threads)
