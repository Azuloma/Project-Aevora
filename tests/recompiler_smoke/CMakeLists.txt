cmake_minimum_required(VERSION 3.20)
project(recompiler_smoke LANGUAGES C CXX)
foreach(path_variable SUYU_SOURCE GENERATED_DIR LOOP_DIR)
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
  add_executable(smoke_topology_unit topology_unit.cpp)
  target_compile_features(smoke_topology_unit PRIVATE cxx_std_20)
  target_include_directories(smoke_topology_unit PRIVATE "${SUYU_SOURCE}/src")
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
  # Exercise the emitter's real multi-module CMake definitions. Rebuilding a
  # hand-written approximation would hide missing per-module symbol aliases.
  set(RECOMP_STATIC_ONLY ON)
  add_subdirectory("${GENERATED_DIR}" generated_static)
  add_subdirectory("${GENERATED_DIR}/second" generated_second_static)
  add_executable(smoke_static static.c)
  target_compile_features(smoke_static PRIVATE c_std_11)
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
    endif()
  endif()
  if(NOT MSVC)
    target_link_libraries(smoke_run PRIVATE m)
    target_link_libraries(smoke_static PRIVATE m)
  endif()
endif()
