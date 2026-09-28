# asura_wrath_recomp - ReXGlue Recompiled Project
cmake_minimum_required(VERSION 3.25)
project(asura_wrath_recomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Keep optimization configuration-aware and preserve guest floating-point
# semantics. The upstream template forced -O3/-ffast-math and AVX2/FMA even
# for Debug builds; none of those ISA extensions are required by ReXGlue.
add_compile_options(
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-funroll-loops>
    -Wno-nan-infinity-disabled
    -Wno-c++98-compat
    -Wno-c++98-compat-pedantic
    -Wno-reserved-identifier
    -Wno-unsafe-buffer-usage
    -Wno-global-constructors
    -DREXGLUE_PROFILE_GUEST_FUNCTIONS
)

# ReXGlue's x86 byte-swap paths use SSSE3 _mm_shuffle_epi8 directly.
# Keep that modest baseline, but do not require AVX2/FMA for the portable build.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  add_compile_options(-mssse3)
endif()

option(ASURA_REQUIRE_AVX2
       "Build the x86-64 executable with an AVX2/FMA CPU requirement" OFF)
if(ASURA_REQUIRE_AVX2 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  add_compile_options(-mavx2 -mfma)
endif()

if(NOT REXSDK_DIR)
  set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue" CACHE PATH "Path to rexglue-sdk source tree")
endif()

if(NOT DEFINED REX_PLATFORM)
  if(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|x86")
      set(REX_PLATFORM "win-amd64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
      set(REX_PLATFORM "win-arm64")
    endif()
  elseif(UNIX AND NOT APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|x86")
      set(REX_PLATFORM "linux-amd64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
      set(REX_PLATFORM "linux-arm64")
    endif()
  elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|x86")
      set(REX_PLATFORM "mac-amd64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
      set(REX_PLATFORM "mac-arm64")
    endif()
  endif()
endif()

include(generated/rexglue.cmake)

if(ANDROID)
  set(ASURAWRATH_SOURCES
    src/main.cpp
    src/jni_input.cpp
  )
else()
  set(ASURAWRATH_SOURCES
    src/main.cpp
  )
endif()

if(WIN32 AND NOT ANDROID)
  enable_language(RC)
  list(APPEND ASURAWRATH_SOURCES
    src/asura_launcher_win.cpp
    src/asura_windows.rc
  )
endif()

if(ANDROID)
  add_library(asura_wrath_recomp SHARED ${ASURAWRATH_SOURCES})
  target_link_libraries(asura_wrath_recomp PRIVATE log SDL3::SDL3)
else()
  add_executable(asura_wrath_recomp ${ASURAWRATH_SOURCES})
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID)
  target_link_options(asura_wrath_recomp PRIVATE "-Wl,-rpath,${REXSDK_DIR}/out/${REX_PLATFORM}:$ORIGIN/../../../tools/rexglue/out/${REX_PLATFORM}:$ORIGIN")
elseif(APPLE)
  target_link_options(asura_wrath_recomp PRIVATE "-Wl,-rpath,${REXSDK_DIR}/out/${REX_PLATFORM}:@executable_path/../../../tools/rexglue/out/${REX_PLATFORM}:@executable_path")
endif()

if(WIN32 AND NOT ANDROID)
  set_target_properties(asura_wrath_recomp PROPERTIES OUTPUT_NAME "Asura's Wrath")
  target_compile_definitions(asura_wrath_recomp PRIVATE ASURA_NATIVE_LAUNCHER=1)
  target_link_libraries(asura_wrath_recomp PRIVATE dxgi comctl32)
else()
  set_target_properties(asura_wrath_recomp PROPERTIES OUTPUT_NAME "asura_wrath_recomp")
endif()

set(REXGLUE_RECOMP_DEBUG_INFO "none" CACHE STRING "Debug info level" FORCE)

rexglue_setup_target(asura_wrath_recomp GPU_PLUGINS xenos)
target_include_directories(asura_wrath_recomp PRIVATE ${REXSDK_DIR}/thirdparty/imgui ${CMAKE_CURRENT_SOURCE_DIR}/src)


# Preservation/offline analysis tool. This is deliberately separate from the
# game executable: it works only on user-owned XEX/XEXP inputs, stages copies in
# a temporary directory, and exports the patched in-memory guest image for
# static analysis without modifying preserved originals.
if(NOT ANDROID)
  add_executable(asura_tu_image_audit
    tools/asura_tu_image_audit.cpp
  )
  target_link_libraries(asura_tu_image_audit PRIVATE rex::runtime)
  target_include_directories(asura_tu_image_audit PRIVATE
    ${REXSDK_DIR}/include
  )
  if(WIN32)
    set_target_properties(asura_tu_image_audit PROPERTIES
      OUTPUT_NAME "Asura TU01 Image Audit"
    )
  endif()
endif()
