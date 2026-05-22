cmake_minimum_required(VERSION 3.14)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR
    "In-source builds are not supported. "
    "You may need to delete 'CMakeCache.txt' and 'CMakeFiles/' before rebuilding this project."
  )
endif()

project(breakpad-cmake C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

  get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  if(NOT isMultiConfig
     AND NOT CMAKE_BUILD_TYPE
     AND NOT CMAKE_CONFIGURATION_TYPES
  )
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
  endif()
endif()

option(BREAKPAD_USE_EXTERNAL "Use external breakpad" ON)
set(BREAKPAD_EXTERNAL_URL "https://chromium.googlesource.com/breakpad/breakpad" CACHE STRING "Download external URL")
set(BREAKPAD_EXTERNAL_TAG "2c736308b5a4c7a8371fa3a3e434f551eddd17c9" CACHE STRING "Download external tag")
if(BREAKPAD_USE_EXTERNAL)
  if(NOT DEFINED ENV{CPM_SOURCE_CACHE})
    set(ENV{CPM_SOURCE_CACHE} ${CMAKE_SOURCE_DIR}/cmake_modules)
  endif()
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/CPM.cmake)
  CPMAddPackage(
    NAME breakpad-external
    GIT_REPOSITORY ${BREAKPAD_EXTERNAL_URL}
    GIT_TAG ${BREAKPAD_EXTERNAL_TAG}
    GIT_SHALLOW FALSE
    DOWNLOAD_ONLY
  )
  set(BREAKPAD_ROOTDIR "${breakpad-external_SOURCE_DIR}")
else()
  set(BREAKPAD_ROOTDIR "${CMAKE_CURRENT_SOURCE_DIR}/breakpad")
endif()
set(BREAKPAD_SRCDIR "${BREAKPAD_ROOTDIR}/src")

file(GLOB BREAKPAD_SRCS
  ${BREAKPAD_SRCDIR}/common/*.cc
  ${BREAKPAD_SRCDIR}/common/*.c
  ${BREAKPAD_SRCDIR}/client/*.cc
)

if(WIN32)
  file(GLOB_RECURSE BREAKPAD_SRCS
    ${BREAKPAD_SRCDIR}/client/windows/*.cc
    ${BREAKPAD_SRCDIR}/common/windows/*.cc
  )
elseif(APPLE)
  file(GLOB_RECURSE BREAKPAD_SRCS
    ${BREAKPAD_SRCDIR}/client/mac/*.cc
    ${BREAKPAD_SRCDIR}/common/mac/*.cc
  )
  list(APPEND BREAKPAD_SRCS ${BREAKPAD_SRCDIR}/common/mac/MachIPC.mm)
else()
  file(GLOB_RECURSE BREAKPAD_SRCS
    ${BREAKPAD_SRCDIR}/client/linux/*.cc
    ${BREAKPAD_SRCDIR}/common/linux/*.cc
  )
endif()

list(FILTER BREAKPAD_SRCS EXCLUDE REGEX "/sender|/tests|/unittests|/testcases")
list(FILTER BREAKPAD_SRCS EXCLUDE REGEX "(_unittest|_test|microdump|synth)\\.cc$")

if(WIN32)
  set(BREAKPAD_EXCLUDE_FILES
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
  string(REPLACE ";" "|" BREAKPAD_EXCLUDE_REGEX "${BREAKPAD_EXCLUDE_FILES}")
  list(FILTER BREAKPAD_SRCS EXCLUDE REGEX "(${BREAKPAD_EXCLUDE_REGEX})\\.cc$")
elseif(NOT APPLE)
  include(CheckSymbolExists)
  check_symbol_exists(getcontext "ucontext.h" HAVE_GETCONTEXT)
  if(NOT HAVE_GETCONTEXT)
    enable_language(ASM)
    list(APPEND BREAKPAD_SRCS ${BREAKPAD_SRCDIR}/common/linux/breakpad_getcontext.S)
  endif()
endif()

if(BUILD_SHARED_LIBS)
  set(BREAKPAD_LIB_TYPE SHARED)
else()
  set(BREAKPAD_LIB_TYPE STATIC)
endif()

add_library(breakpad ${BREAKPAD_LIB_TYPE} ${BREAKPAD_SRCS})
add_library(breakpad::breakpad ALIAS breakpad)
target_include_directories(breakpad PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/linux-syscall-support
  ${BREAKPAD_SRCDIR}
)

file(STRINGS "${BREAKPAD_ROOTDIR}/configure.ac" AC_LINES REGEX "AX_CXX_COMPILE_STDCXX")
foreach(line IN LISTS AC_LINES)
  if((NOT line MATCHES "^[ \t]*(dnl|#)") AND (line MATCHES "AX_CXX_COMPILE_STDCXX\\([ \t]*([0-9]+)"))
    set(BREAKPAD_CXX_STD "${CMAKE_MATCH_1}")
    break()
  endif()
endforeach()
set_target_properties(breakpad PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS ON
  CXX_STANDARD ${BREAKPAD_CXX_STD}
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS ON
)

if((UNIX AND NOT APPLE) OR CMAKE_SYSTEM_NAME MATCHES "Linux|GNU")
  target_compile_definitions(breakpad PRIVATE
    _GNU_SOURCE=1
  )
endif()

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
  find_package(Threads REQUIRED)
  target_link_libraries(breakpad PRIVATE Threads::Threads)
  if(HAVE_GETCONTEXT)
    target_compile_definitions(breakpad PRIVATE HAVE_GETCONTEXT=1)
  endif()
  target_compile_definitions(breakpad PRIVATE HAVE_A_OUT_H)
endif()
