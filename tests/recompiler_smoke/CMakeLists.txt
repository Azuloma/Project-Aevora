cmake_minimum_required(VERSION 3.20)
project(recompiler_smoke LANGUAGES C CXX)
foreach(path_variable SUYU_SOURCE GENERATED_DIR LOOP_DIR XMOD_DIR BL_DIR LOOKUP_DIR)
  if(DEFINED ${path_variable})
    file(TO_CMAKE_PATH "${${path_variable}}" normalized_path)
    set(${path_variable} "${normalized_path}")
  endif()
endforeach()
set(gaps_source "${SUYU_SOURCE}/src/core/arm/recomp/recomp_gaps.cpp")
if(LOOP_DIR)
  # The coverage loop: a generated module driven with the real gap recorder.
  file(GLOB loop_sources "${LOOP_DIR}/src/*.c")
  add_executable(smoke_loop loop_host.cpp loop_c.c ${loop_sources} "${gaps_source}"
      "${LOOP_DIR}/recomp_runtime.c" "${LOOP_DIR}/recomp_export.c")
  target_compile_features(smoke_loop PRIVATE cxx_std_20 c_std_11)
  target_include_directories(smoke_loop PRIVATE "${LOOP_DIR}" "${SUYU_SOURCE}/src")
  target_compile_definitions(smoke_loop PRIVATE SUYU_HOSTED_RECOMP=1 RECOMP_SHARED_MODULE=1)
  if(NOT MSVC)
    target_link_libraries(smoke_loop PRIVATE m)
  endif()
elseif(BL_DIR)
  # The chaining fixture (export.cpp --bl). BL_EXPECT_CHAIN and BL_EXPECT_INDIRECT say
  # whether the module was exported with direct (B/BL) and indirect (RET/BR/BLR) chaining.
  if(NOT DEFINED BL_EXPECT_CHAIN OR NOT DEFINED BL_EXPECT_INDIRECT)
    message(FATAL_ERROR "the chain fixture needs BL_EXPECT_CHAIN and BL_EXPECT_INDIRECT (0 or 1)")
  endif()
  if(NOT DEFINED BL_EXPECT_START)
    set(BL_EXPECT_START 0)
  endif()
  file(GLOB bl_sources "${BL_DIR}/src/*.c")
  add_executable(smoke_bl bl_c.c ${bl_sources}
      "${BL_DIR}/recomp_runtime.c" "${BL_DIR}/recomp_export.c")
  target_compile_features(smoke_bl PRIVATE c_std_11)
  target_include_directories(smoke_bl PRIVATE "${BL_DIR}")
  target_compile_definitions(smoke_bl PRIVATE SUYU_HOSTED_RECOMP=1 RECOMP_SHARED_MODULE=1
      BL_EXPECT_CHAIN=${BL_EXPECT_CHAIN} BL_EXPECT_INDIRECT=${BL_EXPECT_INDIRECT}
      BL_EXPECT_START=${BL_EXPECT_START})
  if(NOT MSVC)
    target_link_libraries(smoke_bl PRIVATE m)
  endif()
elseif(LOOKUP_DIR)
  # The block-index fixture (export.cpp --lookup); lookup_c.c includes the index unit itself.
  file(GLOB lookup_sources "${LOOKUP_DIR}/src/*.c")
  list(FILTER lookup_sources EXCLUDE REGEX "recompiled_lookup\\.c$")
  add_executable(smoke_lookup lookup_c.c ${lookup_sources}
      "${LOOKUP_DIR}/recomp_runtime.c" "${LOOKUP_DIR}/recomp_export.c")
  target_compile_features(smoke_lookup PRIVATE c_std_11)
  target_include_directories(smoke_lookup PRIVATE "${LOOKUP_DIR}" "${SUYU_SOURCE}/src")
  target_compile_definitions(smoke_lookup PRIVATE SUYU_HOSTED_RECOMP=1 RECOMP_SHARED_MODULE=1)
  if(NOT MSVC)
    target_link_libraries(smoke_lookup PRIVATE m)
  endif()
elseif(XMOD_DIR)
  # The three-module split fixture (export.cpp --xmod): module "smoke" and "third" are
  # linked statically into xmod_host, module "second" is built as a shared image that
  # the production bundle loader loads from the build's shared/ directory. Like the
  # identity test, the images carry their identity from compile definitions that run.py
  # takes from the topology header's own conversion.
  if(NOT IDENTITY_BUNDLE_BYTES OR NOT REGISTRY_C)
    message(FATAL_ERROR "the three-module fixture needs IDENTITY_BUNDLE_BYTES and REGISTRY_C")
  endif()
  file(TO_CMAKE_PATH "${REGISTRY_C}" registry_c)
  set(RECOMP_STATIC_ONLY ON)
  add_subdirectory("${XMOD_DIR}" xmod_smoke_static)
  add_subdirectory("${XMOD_DIR}/third" xmod_third_static)
  set(RECOMP_STATIC_ONLY OFF)
  set(RECOMP_BUILD_STATIC_LIB OFF)
  add_subdirectory("${XMOD_DIR}/second" xmod_second_shared)
  if(NOT TARGET recompiled_second)
    message(FATAL_ERROR "Generated module 'second' does not define the shared image target "
                        "'recompiled_second'")
  endif()
  target_compile_definitions(recompiled_second PRIVATE
      RECOMP_IDENTITY_NAME="second" RECOMP_IDENTITY_BUNDLE_BYTES=${IDENTITY_BUNDLE_BYTES})
  set_target_properties(recompiled_second PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/shared"
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/shared")
  # The standalone runner is not part of this test.
  if(TARGET recompiled)
    set_target_properties(recompiled PROPERTIES EXCLUDE_FROM_ALL ON)
  endif()
  add_executable(xmod_host xmod_host.cpp xmod_exec.c "${registry_c}"
      "${SUYU_SOURCE}/src/core/arm/recomp/recomp_bundle.cpp")
  target_compile_features(xmod_host PRIVATE cxx_std_20 c_std_11)
  target_include_directories(xmod_host PRIVATE "${SUYU_SOURCE}/src" "${XMOD_DIR}")
  set_source_files_properties(xmod_exec.c PROPERTIES COMPILE_DEFINITIONS
      "SUYU_HOSTED_RECOMP=1;RECOMP_SHARED_MODULE=1")
  target_link_libraries(xmod_host PRIVATE recomp_static_smoke recomp_static_third)
  if(NOT MSVC)
    target_link_libraries(xmod_host PRIVATE m ${CMAKE_DL_LIBS})
  endif()
  add_dependencies(xmod_host recompiled_second)
elseif(NOT GENERATED_DIR)
  add_executable(smoke_export export.cpp "${gaps_source}")
  target_compile_features(smoke_export PRIVATE cxx_std_20)
  target_include_directories(smoke_export PRIVATE "${SUYU_SOURCE}/src")
  if(MSVC)
    # The emitter's string literals contain UTF-8 text. suyu itself builds with
    # /utf-8 (src/CMakeLists.txt); without it MSVC reads the source in the
    # system code page and the golden-hashed output differs from a real export.
    target_compile_options(smoke_export PRIVATE /utf-8)
  endif()
  add_executable(smoke_gaps_unit gaps_unit.cpp "${gaps_source}")
  target_compile_features(smoke_gaps_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_gaps_unit PRIVATE "${SUYU_SOURCE}/src")
  add_executable(smoke_overlay_logic_unit overlay_logic_unit.cpp)
  target_compile_features(smoke_overlay_logic_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_overlay_logic_unit PRIVATE "${SUYU_SOURCE}/src")
  find_package(Threads REQUIRED)
  target_link_libraries(smoke_overlay_logic_unit PRIVATE Threads::Threads)
  if(NOT NLOHMANN_INCLUDE_DIR)
    message(FATAL_ERROR "NLOHMANN_INCLUDE_DIR is required (run.py passes it)")
  endif()
  add_executable(smoke_build_identity_unit build_identity_unit.cpp
      "${SUYU_SOURCE}/src/suyu_cmd/overlay/build_identity.cpp")
  target_compile_features(smoke_build_identity_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_build_identity_unit PRIVATE "${SUYU_SOURCE}/src" "${NLOHMANN_INCLUDE_DIR}")
  if(MSVC)
    target_compile_options(smoke_build_identity_unit PRIVATE /utf-8)
  endif()
  add_executable(smoke_env_option_unit env_option_unit.cpp)
  target_compile_features(smoke_env_option_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_env_option_unit PRIVATE "${SUYU_SOURCE}/src")
  add_executable(smoke_thread_time_unit thread_time_unit.cpp)
  target_compile_features(smoke_thread_time_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_thread_time_unit PRIVATE "${SUYU_SOURCE}/src")
  add_executable(smoke_guest_time_unit guest_time_unit.cpp)
  target_compile_features(smoke_guest_time_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_guest_time_unit PRIVATE "${SUYU_SOURCE}/src")
  add_executable(smoke_sharded_counters_unit sharded_counters_unit.cpp)
  target_compile_features(smoke_sharded_counters_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_sharded_counters_unit PRIVATE "${SUYU_SOURCE}/src")
  find_package(Threads REQUIRED)
  target_link_libraries(smoke_sharded_counters_unit PRIVATE Threads::Threads)
  add_executable(smoke_lookup_stats_unit lookup_stats_unit.cpp)
  target_compile_features(smoke_lookup_stats_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_lookup_stats_unit PRIVATE "${SUYU_SOURCE}/src")
  target_link_libraries(smoke_lookup_stats_unit PRIVATE Threads::Threads)
  add_executable(smoke_exclusive_stats_unit exclusive_stats_unit.cpp)
  target_compile_features(smoke_exclusive_stats_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_exclusive_stats_unit PRIVATE "${SUYU_SOURCE}/src")
  target_link_libraries(smoke_exclusive_stats_unit PRIVATE Threads::Threads)
  add_executable(smoke_access_stats_unit access_stats_unit.cpp)
  target_compile_features(smoke_access_stats_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_access_stats_unit PRIVATE "${SUYU_SOURCE}/src")
  target_link_libraries(smoke_access_stats_unit PRIVATE Threads::Threads)
  add_executable(smoke_access_sites_unit access_sites_unit.cpp)
  target_compile_features(smoke_access_sites_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_access_sites_unit PRIVATE "${SUYU_SOURCE}/src")
  target_link_libraries(smoke_access_sites_unit PRIVATE Threads::Threads)
  add_executable(smoke_exit_targets_unit exit_targets_unit.cpp)
  target_compile_features(smoke_exit_targets_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_exit_targets_unit PRIVATE "${SUYU_SOURCE}/src")
  target_link_libraries(smoke_exit_targets_unit PRIVATE Threads::Threads)
  add_executable(smoke_chain_start_budget_unit chain_start_budget_unit.cpp)
  target_compile_features(smoke_chain_start_budget_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_chain_start_budget_unit PRIVATE "${SUYU_SOURCE}/src")
  add_executable(smoke_topology_unit topology_unit.cpp)
  target_compile_features(smoke_topology_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_topology_unit PRIVATE "${SUYU_SOURCE}/src")
  add_executable(smoke_xmod_table_unit xmod_table_unit.cpp
      "${SUYU_SOURCE}/src/core/arm/recomp/recomp_bundle.cpp")
  target_compile_features(smoke_xmod_table_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_xmod_table_unit PRIVATE "${SUYU_SOURCE}/src")
  add_executable(smoke_index_report_unit index_report_unit.cpp
      "${SUYU_SOURCE}/src/core/arm/recomp/recomp_bundle.cpp")
  target_compile_features(smoke_index_report_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_index_report_unit PRIVATE "${SUYU_SOURCE}/src")
  add_executable(smoke_index_view_unit index_view_unit.cpp)
  target_compile_features(smoke_index_view_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_index_view_unit PRIVATE "${SUYU_SOURCE}/src")
  add_executable(smoke_aot_cache_unit aot_cache_unit.cpp
      "${SUYU_SOURCE}/src/suyu/aot_export_cache.cpp")
  target_compile_features(smoke_aot_cache_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_aot_cache_unit PRIVATE "${SUYU_SOURCE}/src")
else()
  file(GLOB generated_sources "${GENERATED_DIR}/src/*.c")
  add_executable(smoke_run run.c ${generated_sources}
      "${GENERATED_DIR}/recomp_runtime.c" "${GENERATED_DIR}/recomp_export.c")
  target_compile_features(smoke_run PRIVATE c_std_11)
  target_include_directories(smoke_run PRIVATE "${GENERATED_DIR}")
  target_compile_definitions(smoke_run PRIVATE SUYU_HOSTED_RECOMP=1 RECOMP_SHARED_MODULE=1)
  if(SMOKE_DIRECT_CHAIN)
    target_compile_definitions(smoke_run PRIVATE SMOKE_DIRECT_CHAIN=1)
  endif()
  # Exercise the emitter's real multi-module CMake definitions. Rebuilding a
  # hand-written approximation would hide missing per-module symbol aliases.
  set(RECOMP_STATIC_ONLY ON)
  add_subdirectory("${GENERATED_DIR}" generated_static)
  add_subdirectory("${GENERATED_DIR}/second" generated_second_static)
  add_executable(smoke_static static.c)
  target_compile_features(smoke_static PRIVATE c_std_11)
  target_include_directories(smoke_static PRIVATE "${SUYU_SOURCE}/src")
  target_link_libraries(smoke_static PRIVATE recomp_static_smoke recomp_static_second)
  # ABI 6 images report features; check the host's refusal logic against them.
  file(READ "${GENERATED_DIR}/recomp_export.c" generated_export)
  string(FIND "${generated_export}" "recomp_image_features" has_features)
  if(NOT has_features EQUAL -1)
    add_executable(smoke_features features.cpp ${generated_sources}
        "${GENERATED_DIR}/recomp_runtime.c" "${GENERATED_DIR}/recomp_export.c")
    target_compile_features(smoke_features PRIVATE cxx_std_20 c_std_11)
    target_include_directories(smoke_features PRIVATE "${GENERATED_DIR}" "${SUYU_SOURCE}/src")
    target_compile_definitions(smoke_features PRIVATE SUYU_HOSTED_RECOMP=1 RECOMP_SHARED_MODULE=1)
    if(NOT MSVC)
      target_link_libraries(smoke_features PRIVATE m)
    endif()
  endif()
  # GG1 images: the host manager against the generated guard, including races.
  string(FIND "${generated_export}" "recomp_image_guard_gen_v1" has_guard_gen)
  if(NOT has_guard_gen EQUAL -1)
    find_package(Threads REQUIRED)
    # gg_c.c includes the block unit itself, to read its private seen words.
    set(generated_sources_gg ${generated_sources})
    list(FILTER generated_sources_gg EXCLUDE REGEX "_0[.]c$")
    add_executable(smoke_gg_host gg_host.cpp gg_c.c ${generated_sources_gg}
        "${GENERATED_DIR}/recomp_runtime.c" "${GENERATED_DIR}/recomp_export.c"
        "${SUYU_SOURCE}/src/core/arm/recomp/recomp_guard_gen.cpp")
    target_compile_features(smoke_gg_host PRIVATE cxx_std_20 c_std_11)
    target_include_directories(smoke_gg_host PRIVATE "${GENERATED_DIR}" "${SUYU_SOURCE}/src")
    target_compile_definitions(smoke_gg_host PRIVATE SUYU_HOSTED_RECOMP=1 RECOMP_SHARED_MODULE=1)
    target_link_libraries(smoke_gg_host PRIVATE Threads::Threads)
    # e.g. -DSMOKE_SANITIZE=thread for the race modes under ThreadSanitizer.
    if(SMOKE_SANITIZE AND NOT MSVC)
      target_compile_options(smoke_gg_host PRIVATE -fsanitize=${SMOKE_SANITIZE} -g)
      target_link_options(smoke_gg_host PRIVATE -fsanitize=${SMOKE_SANITIZE})
    endif()
    if(NOT MSVC)
      target_link_libraries(smoke_gg_host PRIVATE m)
    endif()
  endif()
  # Split-export identity (topology v1): the generated image exports
  # recomp_image_identity_v1, whose values come from compile definitions. Every
  # target that compiles recomp_export.c with RECOMP_SHARED_MODULE must define
  # them, and the second module is also built as a real shared image that
  # identity_host loads and queries.
  string(FIND "${generated_export}" "recomp_image_identity_v1" has_identity)
  if(NOT has_identity EQUAL -1)
    # run.py passes the bundle id (hex) and its byte list from the topology
    # header's own conversion (smoke_topology_unit --bundle-bytes).
    if(NOT IDENTITY_BUNDLE_HEX OR NOT IDENTITY_BUNDLE_BYTES)
      message(FATAL_ERROR "the identity export needs IDENTITY_BUNDLE_HEX and IDENTITY_BUNDLE_BYTES")
    endif()
    foreach(identity_target smoke_run smoke_features smoke_gg_host)
      if(TARGET ${identity_target})
        target_compile_definitions(${identity_target} PRIVATE
            RECOMP_IDENTITY_NAME="smoke" RECOMP_IDENTITY_BUNDLE_BYTES=${IDENTITY_BUNDLE_BYTES})
      endif()
    endforeach()
    # The shared target name follows the generator's rule for a non-main module
    # (recomp_topology.h SharedImageTargetName).
    set(identity_shared_target recompiled_second)
    set(RECOMP_STATIC_ONLY OFF)
    set(RECOMP_BUILD_STATIC_LIB OFF)
    add_subdirectory("${GENERATED_DIR}/second" generated_second_shared)
    if(NOT TARGET ${identity_shared_target})
      message(FATAL_ERROR "Generated module 'second' does not define the shared image target "
                          "'${identity_shared_target}'")
    endif()
    target_compile_definitions(${identity_shared_target} PRIVATE
        RECOMP_IDENTITY_NAME="second" RECOMP_IDENTITY_BUNDLE_BYTES=${IDENTITY_BUNDLE_BYTES})
    set_target_properties(${identity_shared_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/shared"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/shared")
    # The standalone runner is not part of this test.
    if(TARGET recompiled)
      set_target_properties(recompiled PROPERTIES EXCLUDE_FROM_ALL ON)
    endif()
    add_executable(identity_host identity_host.c)
    target_compile_features(identity_host PRIVATE c_std_11)
    target_include_directories(identity_host PRIVATE "${SUYU_SOURCE}/src")
    if(NOT WIN32)
      target_link_libraries(identity_host PRIVATE ${CMAKE_DL_LIBS})
    endif()
    add_dependencies(identity_host ${identity_shared_target})
    # The mixed-bundle host: the production bundle loader against a registry v1
    # that run.py writes with the production writer (smoke: static, second: DLL).
    if(REGISTRY_C)
      file(TO_CMAKE_PATH "${REGISTRY_C}" registry_c)
      # bundle_exec.c runs generated blocks of the static and the shared image
      # against one host bridge, with the generated context layout.
      add_executable(bundle_host bundle_host.cpp bundle_exec.c "${registry_c}"
          "${SUYU_SOURCE}/src/core/arm/recomp/recomp_bundle.cpp")
      target_compile_features(bundle_host PRIVATE cxx_std_20 c_std_11)
      target_include_directories(bundle_host PRIVATE "${SUYU_SOURCE}/src" "${GENERATED_DIR}")
      set_source_files_properties(bundle_exec.c PROPERTIES COMPILE_DEFINITIONS
          "SUYU_HOSTED_RECOMP=1;RECOMP_SHARED_MODULE=1")
      target_link_libraries(bundle_host PRIVATE recomp_static_smoke)
      if(NOT MSVC)
        target_link_libraries(bundle_host PRIVATE m ${CMAKE_DL_LIBS})
      endif()
      add_dependencies(bundle_host ${identity_shared_target})
      # A loadable image without the entry points, under the real image's file name.
      add_library(bundle_stub_image SHARED bundle_stub_image.c)
      set_target_properties(bundle_stub_image PROPERTIES OUTPUT_NAME "${identity_shared_target}"
          RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/stub"
          LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/stub")
      add_dependencies(bundle_host bundle_stub_image)
    endif()
  endif()
  if(NOT MSVC)
    target_link_libraries(smoke_run PRIVATE m)
    target_link_libraries(smoke_static PRIVATE m)
  endif()
endif()
