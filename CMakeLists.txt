cmake_minimum_required(VERSION 3.5...3.18 FATAL_ERROR)
project(CAF_INC CXX)

# -- includes ------------------------------------------------------------------

include(CMakePackageConfigHelpers)
include(CheckCXXSourceCompiles)
include(GNUInstallDirs)
include(GenerateExportHeader)

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
  if(CMAKE_VERSION VERSION_LESS 3.13.5)
    message(
      FATAL_ERROR
      "Standalone builds require CMake >= 3.13.5, found ${CMAKE_VERSION}.")
  endif()
  include(FetchContent)
  FetchContent_Declare(
    actor_framework
    GIT_REPOSITORY https://github.com/actor-framework/actor-framework.git
    GIT_TAG        b69b3f9
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
  if(NOT TARGET CAF::core)
    find_package(CAF COMPONENTS core test REQUIRED)
    message(STATUS "Found CAF version ${CAF_VERSION}: ${CAF_DIR}")
  endif()
endif()

# -- set the library version for shared library targets ------------------------

if(CMAKE_HOST_SYSTEM_NAME MATCHES "OpenBSD")
  set(CAF_INC_LIB_VERSION "${CAF_VERSION_MAJOR}.${CAF_VERSION_MINOR}"
      CACHE INTERNAL "The version string used for shared library objects")
else()
  set(CAF_INC_LIB_VERSION "${CAF_VERSION}"
      CACHE INTERNAL "The version string used for shared library objects")
endif()

# -- sanity checks -------------------------------------------------------------

if(MSVC AND CAF_INC_SANITIZERS)
  message(FATAL_ERROR "Sanitizer builds are currently not supported on MSVC")
endif()

# -- unit testing setup --------------------------------------------------------

if(CAF_INC_ENABLE_TESTING)
  enable_testing()
  function(caf_incubator_add_test_suites target)
    foreach(suiteName ${ARGN})
      string(REPLACE "." "/" suitePath ${suiteName})
      target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/test/${suitePath}.cpp")
      add_test(NAME ${suiteName}
               COMMAND ${target} -r300 -n -v5 -s "^${suiteName}$")
    endforeach()
  endfunction()
endif()

# -- make sure we have at least C++17 available --------------------------------

# TODO: simply set CXX_STANDARD when switching to CMake ≥ 3.9.6
function(caf_incubator_set_cxx17_flag)
  if(NOT CMAKE_CROSSCOMPILING)
    try_compile(caf_incubator_has_cxx_17
                "${CMAKE_CURRENT_BINARY_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-compiler-features.cpp")
    if(NOT caf_has_cxx_17)
      if(MSVC)
        set(cxx_flag "/std:c++17")
      else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang"
           AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5)
          set(cxx_flag "-std=c++1z")
        else()
          set(cxx_flag "-std=c++17")
        endif()
      endif()
      # Re-run compiler check.
      try_compile(caf_incubator_has_cxx_17
                  "${CMAKE_CURRENT_BINARY_DIR}"
                  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-compiler-features.cpp"
                  COMPILE_DEFINITIONS "${cxx_flag}"
                  OUTPUT_VARIABLE cxx_check_output)
      if(NOT caf_incubator_has_cxx_17)
        message(FATAL_ERROR "\nFatal error: unable activate C++17 mode!\
                             \nPlease see README.md for supported compilers.\
                             \n\ntry_compile output:\n${cxx_check_output}")
      endif()
      set(CAF_INC_CXX17_FLAG "${cxx_flag}" PARENT_SCOPE)
    endif()
  endif()
endfunction()

# -- utility functions ---------------------------------------------------------

# generates the implementation file for the enum that contains to_string,
# from_string and from_integer
function(caf_incubator_add_enum_type target enum_name)
  string(REPLACE "." "/" path "${enum_name}")
  set(hpp_file "${CMAKE_CURRENT_SOURCE_DIR}/caf/${path}.hpp")
  set(cpp_file "${CMAKE_CURRENT_BINARY_DIR}/src/${path}_strings.cpp")
  set(gen_file "${PROJECT_SOURCE_DIR}/cmake/caf-generate-enum-strings.cmake")
  add_custom_command(OUTPUT "${cpp_file}"
                     COMMAND ${CMAKE_COMMAND}
                       "-DINPUT_FILE=${hpp_file}"
                       "-DOUTPUT_FILE=${cpp_file}"
                       -P "${gen_file}"
                     DEPENDS "${hpp_file}" "${gen_file}")
  target_sources(${target} PRIVATE "${cpp_file}")
endfunction()

# TODO: remove when switching to CMake > 3.12
function(caf_incubator_target_link_libraries target)
  if(CMAKE_VERSION VERSION_LESS 3.12)
    get_target_property(target_type ${target} TYPE)
    if (NOT target_type STREQUAL OBJECT_LIBRARY)
      target_link_libraries(${target} ${ARGN})
    else()
      cmake_parse_arguments(CAF_TARGET_LINK_LIBRARIES "" ""
                            "PUBLIC;PRIVATE;INTERFACE" ${ARGN})
      # If we can't link against it, at least make sure to pull in include paths
      # and compiler options.
      foreach(arg IN LISTS CAF_TARGET_LINK_LIBRARIES_PUBLIC
                           CAF_TARGET_LINK_LIBRARIES_PRIVATE)
        if (TARGET ${arg})
          target_include_directories(
            ${target} PRIVATE
            $<TARGET_PROPERTY:${arg},INTERFACE_INCLUDE_DIRECTORIES>)
          target_compile_options(
            ${target} PRIVATE
            $<TARGET_PROPERTY:${arg},INTERFACE_COMPILE_OPTIONS>)
        endif()
      endforeach()
    endif()
  else()
    target_link_libraries(${target} ${ARGN})
  endif()
endfunction()

function(caf_incubator_export_and_install_lib component)
  add_library(CAF::${component} ALIAS libcaf_${component})
  target_include_directories(libcaf_${component} INTERFACE
                             $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                             $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
                             $<INSTALL_INTERFACE:include>)
  install(TARGETS libcaf_${component}
          EXPORT CAFIncubatorTargets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${component}
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${component}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${component})
  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/caf"
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
          COMPONENT ${component}
          FILES_MATCHING PATTERN "*.hpp")
  get_target_property(lib_type libcaf_${component} TYPE)
  if(NOT lib_type STREQUAL "INTERFACE_LIBRARY")
    set_target_properties(libcaf_${component} PROPERTIES
                          EXPORT_NAME ${component}
                          SOVERSION ${CAF_VERSION}
                          VERSION ${CAF_INC_LIB_VERSION}
                          OUTPUT_NAME caf_${component})
    string(TOUPPER "CAF_${component}_EXPORT" export_macro_name)
    generate_export_header(
      libcaf_${component}
      EXPORT_MACRO_NAME ${export_macro_name}
      EXPORT_FILE_NAME "caf/detail/${component}_export.hpp")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/caf/detail/${component}_export.hpp"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/caf/detail/")
  endif()
endfunction()

# -- convenience function for automating our component setup -------------------

# Usage:
# caf_incubator_add_component(
#   foo
#   DEPENDENCIES
#     INTERFACE
#       ...
#     PUBLIC
#       ...
#     PRIVATE
#       ...
#   HEADERS
#     ...
#   SOURCES
#     ...
#   TEST_SOURCES
#     ...
#   TEST_SUITES
#     ...
# )
function(caf_incubator_add_component name)
  set(varargs DEPENDENCIES HEADERS SOURCES TEST_SOURCES TEST_SUITES ENUM_TYPES)
  cmake_parse_arguments(CAF_INC_ADD_COMPONENT "" "" "${varargs}" ${ARGN})
  if(NOT CAF_INC_ADD_COMPONENT_HEADERS)
    message(FATAL_ERROR "Cannot add CAF component without at least one header.")
  endif()
  foreach(param DEPENDENCIES HEADERS)
    if(NOT CAF_INC_ADD_COMPONENT_${param})
      message(FATAL_ERROR "caf_add_component(): missing parameter ${param}")
    endif()
  endforeach()
  set(pub_lib_target "libcaf_${name}")
  set(tst_bin_target "caf-${name}-test")
  set(targets)
  if(NOT CAF_INC_ADD_COMPONENT_SOURCES)
    list(APPEND targets ${pub_lib_target})
    add_library(${pub_lib_target} INTERFACE)
    if(CAF_INC_ENABLE_TESTING AND CAF_INC_ADD_COMPONENT_TEST_SUITES)
      list(APPEND targets ${tst_bin_target})
      set(targets ${tst_bin_target})
      add_executable(${tst_bin_target}
                     ${CAF_INC_ADD_COMPONENT_TEST_SOURCES})
      target_link_libraries(${tst_bin_target} PRIVATE CAF::test
                            ${CAF_INC_ADD_COMPONENT_DEPENDENCIES})
      target_include_directories(${tst_bin_target} PRIVATE
                                 "${CMAKE_CURRENT_SOURCE_DIR}/test")
      caf_incubator_add_test_suites(${tst_bin_target}
                                    ${CAF_INC_ADD_COMPONENT_TEST_SUITES})
    endif()
  else()
    set(obj_lib_target "libcaf_${name}_obj")
    list(APPEND targets ${pub_lib_target} ${obj_lib_target})
    add_library(${obj_lib_target} OBJECT
                ${CAF_INC_ADD_COMPONENT_HEADERS}
                ${CAF_INC_ADD_COMPONENT_SOURCES})
    set_property(TARGET ${obj_lib_target} PROPERTY POSITION_INDEPENDENT_CODE ON)
    caf_incubator_target_link_libraries(${obj_lib_target}
                                        ${CAF_INC_ADD_COMPONENT_DEPENDENCIES})
    add_library(${pub_lib_target}
                "${PROJECT_SOURCE_DIR}/cmake/dummy.cpp"
                $<TARGET_OBJECTS:${obj_lib_target}>)
    if(CAF_INC_ENABLE_TESTING AND CAF_INC_ADD_COMPONENT_TEST_SUITES)
      list(APPEND targets ${tst_bin_target})
      add_executable(${tst_bin_target}
                     ${CAF_INC_ADD_COMPONENT_TEST_SOURCES}
                     $<TARGET_OBJECTS:${obj_lib_target}>)
      target_link_libraries(${tst_bin_target} PRIVATE CAF::test
                            ${CAF_INC_ADD_COMPONENT_DEPENDENCIES})
      target_include_directories(${tst_bin_target} PRIVATE
                                 "${CMAKE_CURRENT_SOURCE_DIR}/test")
      caf_incubator_add_test_suites(${tst_bin_target}
                                    ${CAF_INC_ADD_COMPONENT_TEST_SUITES})
    endif()
  endif()
  target_link_libraries(${pub_lib_target} ${CAF_INC_ADD_COMPONENT_DEPENDENCIES})
  foreach(target ${targets})
    target_compile_options(${target} PRIVATE "${CAF_INC_CXX17_FLAG}")
    # --> set_property(TARGET ${target} PROPERTY CXX_STANDARD 17)
    target_compile_definitions(${target} PRIVATE "libcaf_${name}_EXPORTS")
    target_include_directories(${target} PRIVATE
                               "${CMAKE_CURRENT_SOURCE_DIR}"
                               "${CMAKE_CURRENT_BINARY_DIR}")
    if(BUILD_SHARED_LIBS)
      set_target_properties(${target} PROPERTIES
                            CXX_VISIBILITY_PRESET hidden
                            VISIBILITY_INLINES_HIDDEN ON)
    endif()
  endforeach()
  caf_incubator_export_and_install_lib(${name})
  if(CAF_INC_ADD_COMPONENT_ENUM_TYPES)
    foreach(enum_name ${CAF_INC_ADD_COMPONENT_ENUM_TYPES})
      if(obj_lib_target)
        caf_incubator_add_enum_type(${obj_lib_target} ${enum_name})
      else()
        caf_incubator_add_enum_type(${pub_lib_target} ${enum_name})
      endif()
    endforeach()
  endif()
endfunction()

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

if(CAF_INC_ENABLE_BB_MODULE)
  add_subdirectory(libcaf_bb)
endif()

if(CAF_INC_ENABLE_EXAMPLES)
  add_subdirectory(examples)
endif()

# -- generate and install .cmake files -----------------------------------------

export(EXPORT CAFIncubatorTargets FILE CAFIncubatorTargets.cmake NAMESPACE CAF::)

install(EXPORT CAFIncubatorTargets
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CAFIncubator"
        NAMESPACE CAF::)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/CAFIncubatorConfigVersion.cmake"
  VERSION ${CAF_VERSION}
  COMPATIBILITY ExactVersion)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CAFIncubatorConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/CAFIncubatorConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CAFIncubator")

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/CAFIncubatorConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/CAFIncubatorConfigVersion.cmake"
  DESTINATION
    "${CMAKE_INSTALL_LIBDIR}/cmake/CAFIncubator")
