cmake_minimum_required(VERSION 3.14)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR
    "In-source builds are not supported. "
    "You may need to delete 'CMakeCache.txt' and 'CMakeFiles/' before rebuilding this project."
  )
endif()

project(breakpad-cmake
  VERSION 1.0.0
  LANGUAGES C CXX
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

  get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  if(NOT _is_multi_config
     AND NOT CMAKE_BUILD_TYPE
     AND NOT CMAKE_CONFIGURATION_TYPES
  )
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
  endif()
endif()

set(BREAKPAD_SOURCE_DIR "" CACHE PATH "Path to an existing breakpad source checkout (overrides download)")
set(BREAKPAD_GIT_REPOSITORY "https://chromium.googlesource.com/breakpad/breakpad" CACHE STRING "Git repository URL to fetch breakpad source from")
set(BREAKPAD_GIT_TAG "2c736308b5a4c7a8371fa3a3e434f551eddd17c9" CACHE STRING "Breakpad git tag or commit hash")
option(BREAKPAD_USE_FETCH "Automatically download breakpad source if BREAKPAD_SOURCE_DIR is not set" ON)

if(BREAKPAD_SOURCE_DIR)
  set(_breakpad_rootdir "${BREAKPAD_SOURCE_DIR}")
  message(STATUS "Using breakpad source at: ${_breakpad_rootdir}")
elseif(BREAKPAD_USE_FETCH)
  if(NOT DEFINED ENV{CPM_SOURCE_CACHE})
    set(ENV{CPM_SOURCE_CACHE} ${CMAKE_SOURCE_DIR}/cmake_modules)
  endif()
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/CPM.cmake)
  CPMAddPackage(
    NAME breakpad-external
    GIT_REPOSITORY ${BREAKPAD_GIT_REPOSITORY}
    GIT_TAG ${BREAKPAD_GIT_TAG}
    GIT_SHALLOW FALSE
    DOWNLOAD_ONLY
  )
  set(_breakpad_rootdir "${breakpad-external_SOURCE_DIR}")
else()
  set(_breakpad_rootdir "${CMAKE_CURRENT_SOURCE_DIR}/breakpad")
  message(STATUS "Using local breakpad source at: ${_breakpad_rootdir}")
endif()

if(NOT EXISTS "${_breakpad_rootdir}/src")
  message(FATAL_ERROR "breakpad source not found at ${_breakpad_rootdir}.")
endif()
set(_breakpad_srcdir "${_breakpad_rootdir}/src")

file(GLOB _breakpad_srcs
  ${_breakpad_srcdir}/common/*.cc
  ${_breakpad_srcdir}/client/*.cc
)

if(WIN32)
  file(GLOB_RECURSE _breakpad_platform_srcs
    ${_breakpad_srcdir}/client/windows/*.cc
    ${_breakpad_srcdir}/common/windows/*.cc
  )
  list(APPEND _breakpad_srcs ${_breakpad_platform_srcs})
elseif(APPLE)
  file(GLOB_RECURSE _breakpad_platform_srcs
    ${_breakpad_srcdir}/client/mac/*.cc
    ${_breakpad_srcdir}/common/mac/*.cc
  )
  list(APPEND _breakpad_srcs ${_breakpad_platform_srcs})

  enable_language(OBJCXX)
  list(APPEND _breakpad_srcs ${_breakpad_srcdir}/common/mac/MachIPC.mm)
else()
  file(GLOB_RECURSE _breakpad_platform_srcs
    ${_breakpad_srcdir}/client/linux/*.cc
    ${_breakpad_srcdir}/common/linux/*.cc
  )
  list(APPEND _breakpad_srcs ${_breakpad_platform_srcs})
endif()

list(FILTER _breakpad_srcs EXCLUDE REGEX "/sender|/tests|/unittests|/testcases")
list(FILTER _breakpad_srcs EXCLUDE REGEX "(_unittest|_test|microdump|minidump|synth|upload).*\\.cc$")

if(WIN32)
  set(_breakpad_exclude_files
    "language"
    "path_helper"
    "stabs_to_module"
    "stabs_reader"
    "minidump_file_writer"
    "dia_util"
    "pdb_source_line_writer"
    "omap"
    "pe_util"
    "pe_source_line_writer"
    "module_info"
    "sym_upload_v2_protocol"
    "symbol_collector_client"
    "http_upload"
  )
  string(JOIN "|" _breakpad_exclude_regex ${_breakpad_exclude_files})
  list(FILTER _breakpad_srcs EXCLUDE REGEX "(${_breakpad_exclude_regex})\\.cc$")
elseif(NOT APPLE)
  include(CheckSymbolExists)
  check_symbol_exists(getcontext "ucontext.h" HAVE_GETCONTEXT)
  if(NOT HAVE_GETCONTEXT)
    enable_language(ASM)
    list(APPEND _breakpad_srcs ${_breakpad_srcdir}/common/linux/breakpad_getcontext.S)
  endif()
endif()

if(BUILD_SHARED_LIBS)
  add_library(breakpad SHARED ${_breakpad_srcs})
else()
  add_library(breakpad STATIC ${_breakpad_srcs})
endif()
add_library(breakpad::breakpad ALIAS breakpad)
target_include_directories(breakpad PUBLIC ${_breakpad_srcdir})

file(STRINGS "${_breakpad_rootdir}/configure.ac" _breakpad_ac_lines REGEX "AX_CXX_COMPILE_STDCXX")
foreach(_line IN LISTS _breakpad_ac_lines)
  if((NOT _line MATCHES "^[ \t]*(dnl|#)") AND (_line MATCHES "AX_CXX_COMPILE_STDCXX[ \t]*\\([ \t]*([0-9]+)"))
    set(_breakpad_cxx_std "${CMAKE_MATCH_1}")
    break()
  endif()
endforeach()
if(NOT _breakpad_cxx_std)
  message(WARNING "Missing C++ standard info in configure.ac. Falling back to C++98.")
  set(_breakpad_cxx_std 98)
endif()
set_target_properties(breakpad PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS ON
  CXX_STANDARD ${_breakpad_cxx_std}
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS ON
)
target_compile_features(breakpad PUBLIC cxx_std_${_breakpad_cxx_std})

if(WIN32)
  target_compile_definitions(breakpad PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_DEPRECATE
  )
  if(MINGW)
    target_compile_definitions(breakpad PRIVATE
      __USE_MINGW_ANSI_STDIO=1
    )
  endif()
  if(MSVC)
    target_compile_options(breakpad PRIVATE 
      /utf-8
    )
  endif()
  target_link_libraries(breakpad PRIVATE wininet.lib dbghelp.lib rpcrt4.lib)
elseif(APPLE)
  find_library(CoreFoundation_FRAMEWORK CoreFoundation)
  target_link_libraries(breakpad PRIVATE ${CoreFoundation_FRAMEWORK})
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(breakpad PRIVATE USE_PROTECTED_ALLOCATIONS=0)
  endif()
  target_compile_definitions(breakpad PRIVATE HAVE_MACH_O_NLIST_H)
else()
  target_include_directories(breakpad PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/linux-syscall-support)

  find_package(Threads REQUIRED)
  target_link_libraries(breakpad PRIVATE Threads::Threads)

  if(HAVE_GETCONTEXT)
    target_compile_definitions(breakpad PRIVATE HAVE_GETCONTEXT=1)
  endif()

  include(CheckIncludeFile)
  check_include_file("a.out.h" HAVE_A_OUT_H)
  if(HAVE_A_OUT_H)
    target_compile_definitions(breakpad PRIVATE HAVE_A_OUT_H)
  endif()
endif()
