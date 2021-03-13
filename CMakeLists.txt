cmake_minimum_required(VERSION 3.13.5...3.18 FATAL_ERROR)
project(CAF_INC CXX)

# -- includes ------------------------------------------------------------------

include(CMakePackageConfigHelpers)
include(CheckCXXSourceCompiles)
include(FetchContent)
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
  FetchContent_Declare(
    actor_framework
    GIT_REPOSITORY https://github.com/actor-framework/actor-framework.git
    GIT_TAG        1b29bf10
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
               COMMAND ${target} -r300 -n -v5 -s"^${suiteName}$")
    endforeach()
  endfunction()
endif()

# -- utility targets -----------------------------------------------------------

if(CAF_INC_ENABLE_UTILITY_TARGETS)
  add_executable(caf-generate-enum-strings
                 EXCLUDE_FROM_ALL
                 cmake/caf-generate-enum-strings.cpp)
  set_property(TARGET caf-generate-enum-strings PROPERTY CXX_STANDARD 17)
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

# -- utility functions ---------------------------------------------------------

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
  set(varargs DEPENDENCIES HEADERS SOURCES TEST_SOURCES TEST_SUITES
              ENUM_CONSISTENCY_CHECKS)
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
  set(obj_lib_target "libcaf_${name}_obj")
  set(tst_bin_target "caf-${name}-test")
  if(NOT CAF_INC_ADD_COMPONENT_SOURCES)
    # header-only library
    add_library(${pub_lib_target} INTERFACE)
    if(CAF_INC_ENABLE_TESTING AND CAF_INC_ADD_COMPONENT_TEST_SOURCES)
      set(targets ${tst_bin_target})
      add_executable(${tst_bin_target}
                     ${CAF_INC_ADD_COMPONENT_TEST_SOURCES})
      target_link_libraries(${tst_bin_target} PRIVATE CAF::test
                            ${CAF_INC_ADD_COMPONENT_DEPENDENCIES})
      target_include_directories(${tst_bin_target} PRIVATE
                                 "${CMAKE_CURRENT_SOURCE_DIR}/test")
      if(CAF_INC_ADD_COMPONENT_TEST_SUITES)
        caf_incubator_add_test_suites(${tst_bin_target}
                                      ${CAF_INC_ADD_COMPONENT_TEST_SUITES})
      endif()
    endif()
  elseif(CAF_INC_ENABLE_TESTING AND CAF_INC_ADD_COMPONENT_TEST_SOURCES)
    set(targets ${pub_lib_target} ${obj_lib_target} ${tst_bin_target})
    add_library(${obj_lib_target} OBJECT
                ${CAF_INC_ADD_COMPONENT_HEADERS}
                ${CAF_INC_ADD_COMPONENT_SOURCES})
    set_property(TARGET ${obj_lib_target} PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(${obj_lib_target}
                          ${CAF_INC_ADD_COMPONENT_DEPENDENCIES})
    add_library(${pub_lib_target}
                "${PROJECT_SOURCE_DIR}/cmake/dummy.cpp"
                $<TARGET_OBJECTS:${obj_lib_target}>)
    add_executable(${tst_bin_target}
                   ${CAF_INC_ADD_COMPONENT_TEST_SOURCES}
                   $<TARGET_OBJECTS:${obj_lib_target}>)
    target_link_libraries(${tst_bin_target} PRIVATE CAF::test
                          ${CAF_INC_ADD_COMPONENT_DEPENDENCIES})
    target_include_directories(${tst_bin_target} PRIVATE
                               "${CMAKE_CURRENT_SOURCE_DIR}/test")
    if(CAF_INC_ADD_COMPONENT_TEST_SUITES)
      caf_incubator_add_test_suites(${tst_bin_target}
                                    ${CAF_INC_ADD_COMPONENT_TEST_SUITES})
    endif()
  else()
    set(targets ${pub_lib_target})
    add_library(${pub_lib_target}
                ${CAF_INC_ADD_COMPONENT_HEADERS}
                ${CAF_INC_ADD_COMPONENT_SOURCES})
    set_property(TARGET ${pub_lib_target} PROPERTY POSITION_INDEPENDENT_CODE ON)
  endif()
  target_link_libraries(${pub_lib_target} ${CAF_INC_ADD_COMPONENT_DEPENDENCIES})
  foreach(target ${targets})
    set_property(TARGET ${target} PROPERTY CXX_STANDARD 17)
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
  if(CAF_INC_ADD_COMPONENT_ENUM_CONSISTENCY_CHECKS)
    foreach(enum_name ${CAF_INC_ADD_COMPONENT_ENUM_CONSISTENCY_CHECKS})
      string(REPLACE "." "/" path "${enum_name}")
      caf_incubator_add_enum_consistency_check("caf/${path}.hpp"
                                               "src/${path}_strings.cpp")
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
