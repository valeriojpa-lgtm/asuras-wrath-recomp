# asura_wrath_recomp - ReXGlue Recompiled Project
cmake_minimum_required(VERSION 3.25)
project(asura_wrath_recomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(
    -O3
    -ffast-math
    -funroll-loops
    -Wno-nan-infinity-disabled
    -Wno-c++98-compat
    -Wno-c++98-compat-pedantic
    -Wno-reserved-identifier
    -Wno-unsafe-buffer-usage
    -Wno-global-constructors
    -DREXGLUE_PROFILE_GUEST_FUNCTIONS
)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  add_compile_options(-mssse3 -mavx2 -mfma)
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

# T07: project-owned timing core is temporarily linked into rexruntime so the
# Xbox timing ABI can delegate to Theseus without reversing the DLL dependency.
if(TARGET rexruntime)
  target_sources(rexruntime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat/theseus_timing_bridge.cpp"
  )
  target_include_directories(rexruntime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
endif()
if(TARGET rexcore)
  target_sources(rexcore PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat/theseus_sync_bridge.cpp"
  )
  target_include_directories(rexcore PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
endif()

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

# Theseus host platform boundary. This file must stay free of direct ReXGlue
# dependencies so individual services can migrate to native PC backends.
list(APPEND ASURAWRATH_SOURCES
  src/platform/theseus_platform.cpp
  src/platform/theseus_config.cpp
  src/platform/theseus_filesystem.cpp
  src/platform/theseus_saves.cpp
  src/platform/theseus_input.cpp
  src/platform/theseus_audio.cpp
  src/platform/theseus_timing.cpp
  src/platform/theseus_threading.cpp
  src/platform/theseus_crash.cpp
  src/platform/theseus_stability_guard.cpp
  src/compat/rexglue_filesystem_bridge.cpp
)

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
  set_target_properties(asura_wrath_recomp PROPERTIES
    OUTPUT_NAME "Asura's Wrath"
    WIN32_EXECUTABLE TRUE
  )
  target_compile_definitions(asura_wrath_recomp PRIVATE ASURA_NATIVE_LAUNCHER=1)
  target_link_libraries(asura_wrath_recomp PRIVATE dxgi comctl32 dbghelp psapi)
else()
  set_target_properties(asura_wrath_recomp PROPERTIES OUTPUT_NAME "asura_wrath_recomp")
endif()

set(REXGLUE_RECOMP_DEBUG_INFO "none" CACHE STRING "Debug info level" FORCE)

rexglue_setup_target(asura_wrath_recomp GPU_PLUGINS xenos)
target_include_directories(asura_wrath_recomp PRIVATE ${REXSDK_DIR}/thirdparty/imgui ${CMAKE_CURRENT_SOURCE_DIR}/src)
