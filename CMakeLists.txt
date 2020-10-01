cmake_minimum_required(VERSION 3.13.5 FATAL_ERROR)
project(CAF_INC CXX)

set(CMAKE_CXX_STANDARD 17)

# -- includes ------------------------------------------------------------------

include(CMakePackageConfigHelpers) # For creating .cmake files
include(CheckCXXSourceCompiles)    # Check wether compiler works
include(FetchContent)              # For bundling CAF with the incubator
include(GNUInstallDirs)            # Sets default install paths
include(GenerateExportHeader)      # Auto-generates dllexport macros

# -- override CMake defaults for internal cache entries ------------------------

set(CMAKE_EXPORT_COMPILE_COMMANDS ON
    CACHE INTERNAL "Write JSON compile commands database")
# -- general options -----------------------------------------------------------

option(BUILD_SHARED_LIBS "Build shared library targets" ON)

# -- incubator options that are off by default ---------------------------------

option(CAF_INC_ENABLE_UTILITY_TARGETS
       "Include targets like consistency-check" OFF)
option(CAF_INC_ENABLE_STANDALONE_BUILD
       "Fetch and bulid required CAF modules" OFF)

# -- incubator options that are on by default ----------------------------------

option(CAF_INC_ENABLE_TESTING "Build unit test suites" ON)
option(CAF_INC_ENABLE_NET_MODULE "Build networking module" ON)
option(CAF_INC_ENABLE_BB_MODULE "Build building blocks module" ON)
option(CAF_INC_ENABLE_EXAMPLES "Build small programs" ON)

# -- incubator options with non-boolean values ---------------------------------

set(CAF_INC_SANITIZERS "" CACHE STRING
    "Comma separated sanitizers, e.g., 'address,undefined'")

# -- macOS-specific options ----------------------------------------------------

if(APPLE)
  set(CMAKE_MACOSX_RPATH ON CACHE INTERNAL "Use rpaths on macOS and iOS")
endif()

# -- get dependencies ----------------------------------------------------------

if(CAF_INC_ENABLE_STANDALONE_BUILD)
  FetchContent_Declare(
    actor_framework
    GIT_REPOSITORY https://github.com/actor-framework/actor-framework.git
    GIT_TAG        4f8609b
  )
  FetchContent_Populate(actor_framework)
  set(CAF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(CAF_ENABLE_IO_MODULE OFF CACHE BOOL "" FORCE)
  set(CAF_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(CAF_ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
  set(CAF_ENABLE_OPENSSL_MODULE OFF CACHE BOOL "" FORCE)
  set(CAF_SANITIZERS "${CAF_INC_SANITIZERS}" CACHE STRING "" FORCE)
  add_subdirectory(${actor_framework_SOURCE_DIR} ${actor_framework_BINARY_DIR})
else()
  find_package(CAF COMPONENTS core test REQUIRED)
  message(STATUS "Found CAF version ${CAF_VERSION}: ${CAF_DIR}")
endif()

# -- set the library version for shared library targets ------------------------
if(CMAKE_HOST_SYSTEM_NAME MATCHES "OpenBSD")
  set(CAF_LIB_VERSION "${CAF_VERSION_MAJOR}.${CAF_VERSION_MINOR}"
      CACHE INTERNAL "The version string used for shared library objects")
else()
  set(CAF_LIB_VERSION "${CAF_VERSION}"
      CACHE INTERNAL "The version string used for shared library objects")
endif()

# -- sanity checks -------------------------------------------------------------

if(MSVC AND CAF_INC_SANITIZERS)
  message(FATAL_ERROR "Sanitizer builds are currently not supported on MSVC")
endif()

# -- compiler setup ------------------------------------------------------------

function(caf_incubator_set_default_properties)
  foreach(target ${ARGN})
    if(MSVC)
      # Disable 4275 and 4251 (warnings regarding C++ classes at ABI boundaries).
      target_compile_options(${target} PRIVATE /wd4275 /wd4251)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang"
           OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
      # Flags for both compilers.
      target_compile_options(${target} PRIVATE
                             -ftemplate-depth=512 -ftemplate-backtrace-limit=0
                             -Wall -Wextra -pedantic)
      if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Flags for Clang only.
        target_compile_options(${target} PRIVATE -Wdocumentation)
      else()
        # Flags for GCC only.
        target_compile_options(${target} PRIVATE
                               -Wno-missing-field-initializers)
      endif()
    endif()
    if(CAF_INC_SANITIZERS)
      target_compile_options(${target} PRIVATE
                             -fsanitize=${CAF_INC_SANITIZERS}
                             -fno-omit-frame-pointer)
      target_link_libraries(${target} PRIVATE
                            -fsanitize=${CAF_INC_SANITIZERS}
                            -fno-omit-frame-pointer)
    endif()
  endforeach()
endfunction()

# -- unit testing setup / caf_add_test_suites function  ------------------------

if(CAF_INC_ENABLE_TESTING)
  enable_testing()
  function(caf_incubator_add_test_suites target)
    foreach(suiteName ${ARGN})
      string(REPLACE "." "/" suitePath ${suiteName})
      target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/test/${suitePath}.cpp")
      add_test(NAME ${suiteName}
               COMMAND ${target} -r300 -n -v5 -s"^${suiteName}$")
    endforeach()
  endfunction()
endif()

# -- set default visibility to hidden when building shared libs ----------------

if(BUILD_SHARED_LIBS)
  set(CMAKE_CXX_VISIBILITY_PRESET hidden)
  set(CMAKE_VISIBILITY_INLINES_HIDDEN yes)
  if(POLICY CMP0063)
    cmake_policy(SET CMP0063 NEW)
  endif()
endif()

# -- utility targets -----------------------------------------------------------

if(CAF_INC_ENABLE_UTILITY_TARGETS)
  add_executable(caf-generate-enum-strings
                 EXCLUDE_FROM_ALL
                 cmake/caf-generate-enum-strings.cpp)
  add_custom_target(consistency-check)
  add_custom_target(update-enum-strings)
  # adds a consistency check that verifies that `cpp_file` is still valid by
  # re-generating the file and comparing it to the existing file
  function(caf_incubator_add_enum_consistency_check hpp_file cpp_file)
    set(input "${CMAKE_CURRENT_SOURCE_DIR}/${hpp_file}")
    set(file_under_test "${CMAKE_CURRENT_SOURCE_DIR}/${cpp_file}")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/check/${cpp_file}")
    get_filename_component(output_dir "${output}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_dir}")
    add_custom_command(OUTPUT "${output}"
                       COMMAND caf-generate-enum-strings "${input}" "${output}"
                       DEPENDS caf-generate-enum-strings "${input}")
    get_filename_component(target_name "${input}" NAME_WE)
    add_custom_target("${target_name}"
                      COMMAND
                        "${CMAKE_COMMAND}"
                        "-Dfile_under_test=${file_under_test}"
                        "-Dgenerated_file=${output}"
                        -P "${PROJECT_SOURCE_DIR}/cmake/check-consistency.cmake"
                      DEPENDS "${output}")
    add_dependencies(consistency-check "${target_name}")
    add_custom_target("${target_name}-update"
                      COMMAND
                        caf-generate-enum-strings
                        "${input}"
                        "${file_under_test}"
                       DEPENDS caf-generate-enum-strings "${input}")
    add_dependencies(update-enum-strings "${target_name}-update")
  endfunction()
else()
  function(caf_incubator_add_enum_consistency_check hpp_file cpp_file)
    # nop
  endfunction()
endif()

# -- provide an uinstall target ------------------------------------------------

# Process cmake_uninstall.cmake.in.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
               "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
               IMMEDIATE @ONLY)

# Add uninstall target if it does not exist yet.
if(NOT TARGET uninstall)
  add_custom_target(uninstall)
endif()

add_custom_target(caf-incubator-uninstall)
add_custom_command(TARGET caf-incubator-uninstall
                   PRE_BUILD
                   COMMAND "${CMAKE_COMMAND}" -P
                   "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")

add_dependencies(uninstall caf-incubator-uninstall)

# -- build all components the user asked for -----------------------------------

if(CAF_INC_ENABLE_NET_MODULE)
  add_subdirectory(libcaf_net)
endif()

if(CAF_INC_ENABLE_NET_MODULE)
  add_subdirectory(libcaf_bb)
endif()

if(CAF_INC_ENABLE_EXAMPLES)
  add_subdirectory(examples)
endif()
