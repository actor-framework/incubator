# -- project setup -------------------------------------------------------------

cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(caf_incubator C CXX)

# -- CMake includes for C/C++ features -----------------------------------------

include(CheckCSourceCompiles)
include(CheckCSourceRuns)

# -- set useful CMake options --------------------------------------------------

if(NOT CMAKE_CROSSCOMPILING)
  # Check whether the user already provided flags that enable C++ >= 17.
  try_compile(caf_has_cxx_17
              "${CMAKE_CURRENT_BINARY_DIR}"
              "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-compiler-features.cpp")
  # Try enabling C++17 mode if user-provided flags aren't sufficient.
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
    try_compile(caf_has_cxx_17
                "${CMAKE_CURRENT_BINARY_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-compiler-features.cpp"
                COMPILE_DEFINITIONS "${cxx_flag}"
                OUTPUT_VARIABLE cxx_check_output)
    if(NOT caf_has_cxx_17)
      MESSAGE(FATAL_ERROR "\nFatal error: unable activate C++17 mode!\
                           \nPlease see README.md for supported compilers.\
                           \n\ntry_compile output:\n${cxx_check_output}")
    endif()
    add_compile_options("${cxx_flag}")
  endif()
endif()

# Be nice to VIM users and Clang tools.
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

# Silence policy CMP0042 warning by enabling RPATH explicitly.
if(APPLE AND NOT DEFINED CMAKE_MACOSX_RPATH)
  set(CMAKE_MACOSX_RPATH true)
endif()

# -- helper targets ------------------------------------------------------------

# Simplifies re-running configuration setup.
add_custom_target(configure COMMAND ${CMAKE_CURRENT_BINARY_DIR}/config.status)

# -- check for static builds ---------------------------------------------------

# Shared libs are currently not supported on Windows.
if(WIN32 AND NOT CAF_BUILD_STATIC_ONLY)
  message(STATUS "CAF currently only supports static-only builds on Windows")
  set(CAF_BUILD_STATIC_ONLY yes)
endif()

if(CAF_BUILD_STATIC_RUNTIME)
    set(flags_configs
        CMAKE_CXX_FLAGS
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_CXX_FLAGS_MINSIZEREL
        CMAKE_C_FLAGS
        CMAKE_C_FLAGS_DEBUG
        CMAKE_C_FLAGS_RELEASE
        CMAKE_C_FLAGS_RELWITHDEBINFO
        CMAKE_C_FLAGS_MINSIZEREL
        )
  foreach(flags ${flags_configs})
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      if(NOT ${flags} MATCHES "-static-libstdc\\+\\+")
        set(${flags} "${${flags}} -static-libstdc++")
      endif()
      if(NOT ${flags} MATCHES "-static-libgcc")
        set(${flags} "${${flags}} -static-libgcc")
      endif()
    elseif(MSVC)
      if(${flags} MATCHES "/MD")
        string(REGEX REPLACE "/MD" "/MT" ${flags} "${${flags}}")
      endif()
    endif()
  endforeach()
else()
  set(CAF_BUILD_STATIC_RUNTIME no)
endif()

# -- utility functions ---------------------------------------------------------

# Appends `str` to the variable named `var` with a whitespace as separator.
# Suppresses a leading whitespace if the variable is empty and does nothing if
# `str` is empty.
function(build_string var str)
  if(NOT str STREQUAL "")
    if("${${var}}" STREQUAL "")
      set("${var}" "${str}" PARENT_SCOPE)
    else()
      set("${var}" "${${var}} ${str}" PARENT_SCOPE)
    endif()
  endif()
endfunction(build_string)

# Forces `var` to 'no' if the content of the variables evaluates to false.
function(pretty_no var)
  if(NOT "${${var}}")
    set("${var}" no PARENT_SCOPE)
  endif()
endfunction(pretty_no)

# Forces `var` to 'yes' if the content of the variables evaluates to false.
function(pretty_yes var)
  if("${${var}}")
    set("${var}" yes PARENT_SCOPE)
  endif()
endfunction(pretty_yes)

add_executable(caf-generate-enum-strings cmake/caf-generate-enum-strings.cpp)

function(enum_to_string relative_input_file relative_output_file)
  set(input "${CMAKE_CURRENT_SOURCE_DIR}/${relative_input_file}")
  set(output "${CMAKE_CURRENT_BINARY_DIR}/${relative_output_file}")
  add_custom_command(OUTPUT "${output}"
                     COMMAND caf-generate-enum-strings "${input}" "${output}"
                     DEPENDS caf-generate-enum-strings "${input}")
endfunction()

# -- binary and library path setup ---------------------------------------------

# Prohibit in-source builds.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
    message(FATAL_ERROR "In-source builds are not allowed. Please use "
                        "./configure to choose a build directory and "
                        "initialize the build configuration.")
endif()

# Set module path appropriately.
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Set binary output path if not defined by user.
if(EXECUTABLE_OUTPUT_PATH STREQUAL "")
  set(EXECUTABLE_OUTPUT_PATH "${CMAKE_CURRENT_BINARY_DIR}/bin")
endif()

# Set library output path if not defined by user, but always set library output
# path to binary output path for Xcode projects.
if(CMAKE_GENERATOR STREQUAL "Xcode")
  set(LIBRARY_OUTPUT_PATH "${EXECUTABLE_OUTPUT_PATH}")
elseif(LIBRARY_OUTPUT_PATH STREQUAL "")
  set(LIBRARY_OUTPUT_PATH "${CMAKE_CURRENT_BINARY_DIR}/lib")
endif()

# -- get dependencies ----------------------------------------------------------

find_package(CAF COMPONENTS core io test)

# -- compiler setup ------------------------------------------------------------

# Enable a ton of warnings if --more-clang-warnings is used.
if(CAF_MORE_WARNINGS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(WFLAGS "-Weverything -Wno-c++98-compat -Wno-padded "
               "-Wno-documentation-unknown-command -Wno-exit-time-destructors "
               "-Wno-global-constructors -Wno-missing-prototypes "
               "-Wno-c++98-compat-pedantic -Wno-unused-member-function "
               "-Wno-unused-const-variable -Wno-switch-enum "
               "-Wno-abstract-vbase-init -Wno-shadow "
               "-Wno-missing-noreturn -Wno-covered-switch-default")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(WFLAGS "-Waddress -Wall -Warray-bounds "
               "-Wattributes -Wbuiltin-macro-redefined -Wcast-align "
               "-Wcast-qual -Wchar-subscripts -Wclobbered -Wcomment "
               "-Wconversion -Wconversion-null -Wcoverage-mismatch "
               "-Wcpp -Wdelete-non-virtual-dtor -Wdeprecated "
               "-Wdeprecated-declarations -Wdiv-by-zero -Wdouble-promotion "
               "-Wempty-body -Wendif-labels -Wenum-compare -Wextra "
               "-Wfloat-equal -Wformat -Wfree-nonheap-object "
               "-Wignored-qualifiers -Winit-self "
               "-Winline -Wint-to-pointer-cast -Winvalid-memory-model "
               "-Winvalid-offsetof -Wlogical-op -Wmain -Wmaybe-uninitialized "
               "-Wmissing-braces -Wmultichar "
               "-Wnarrowing -Wnoexcept -Wnon-template-friend "
               "-Wnon-virtual-dtor -Wnonnull -Woverflow "
               "-Woverlength-strings -Wparentheses "
               "-Wpmf-conversions -Wpointer-arith -Wreorder "
               "-Wreturn-type -Wsequence-point "
               "-Wsign-compare -Wswitch -Wtype-limits -Wundef "
               "-Wuninitialized -Wunused -Wvla -Wwrite-strings")
  endif()
  # convert CMake list to a single string, erasing the ";" separators
  string(REPLACE ";" "" WFLAGS_STR ${WFLAGS})
  build_string("EXTRA_FLAGS" "${WFLAGS_STR}")
endif()

# Enable ASAN if requested by the user.
if(CAF_ENABLE_ADDRESS_SANITIZER AND NOT WIN32)
  add_compile_options("-fsanitize=address"
                      "-fno-omit-frame-pointer")
  list(APPEND CAF_EXTRA_LDFLAGS "-fsanitize=address")
endif()

# -pthread is ignored on MacOSX but required on other platforms
if(NOT APPLE AND NOT WIN32)
    build_string("EXTRA_FLAGS" "-pthread")
endif()

# -fPIC generates warnings on MinGW and Cygwin plus extra setup steps needed on MinGW
if(MINGW)
  add_definitions(-D_WIN32_WINNT=0x0600)
  add_definitions(-DWIN32)
  include(GenerateExportHeader)
  list(APPEND CAF_EXTRA_LDFLAGS -lws2_32 -liphlpapi -lpsapi)
  # build static to avoid runtime dependencies to GCC libraries
  build_string("EXTRA_FLAGS" "-static")
elseif(CYGWIN)
  build_string("EXTRA_FLAGS" "-U__STRICT_ANSI__")
else()
  build_string("EXTRA_FLAGS" "-fPIC")
endif()

# Add Windows-specific linker flags.
if (WIN32)
  list(APPEND CAF_EXTRA_LDFLAGS ws2_32 iphlpapi)
endif()

# Support macOS/iOS-specific magic.
if(CAF_OSX_SYSROOT)
  set(CMAKE_OSX_SYSROOT "${CAF_OSX_SYSROOT}")
endif()

# Add iOS target if requested by user.
if(CAF_IOS_DEPLOYMENT_TARGET)
  if(CAF_OSX_SYSROOT STREQUAL "iphonesimulator")
    build_string("EXTRA_FLAGS"
                 "-mios-simulator-version-min=${CAF_IOS_DEPLOYMENT_TARGET}")
  else()
    build_string("EXTRA_FLAGS"
                 "-miphoneos-version-min=${CAF_IOS_DEPLOYMENT_TARGET}")
  endif()
endif()

# -- check if the user provided CXXFLAGS, set defaults otherwise ---------------

if(NOT CMAKE_CXX_FLAGS)
  set(CMAKE_CXX_FLAGS "-Wextra -Wall -pedantic ${EXTRA_FLAGS}")
endif()

if(NOT CMAKE_CXX_FLAGS_DEBUG)
  set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
endif()

if(NOT CMAKE_CXX_FLAGS_MINSIZEREL)
  set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os")
endif()

if(NOT CMAKE_CXX_FLAGS_RELEASE)
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()

if(NOT CMAKE_CXX_FLAGS_RELWITHDEBINFO)
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -fno-omit-frame-pointer")
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# needed by subprojects
if (DEFINED CMAKE_LD_LIBS)
  list(APPEND ${CMAKE_LD_LIBS})
endif()

# -- install targets -----------------------------------------------------------

# Process cmake_uninstall.cmake.in.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
               "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
               IMMEDIATE @ONLY)

# Add uninstall target.
add_custom_target(uninstall
                  COMMAND "${CMAKE_COMMAND}" -P
                  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")

# -- utility macro for adding unit tests ---------------------------------------

if(NOT CAF_NO_UNIT_TESTS)
  enable_testing()
  macro(add_unit_tests globstr)
    file(GLOB_RECURSE tests "${globstr}")
    set(CAF_ALL_UNIT_TESTS ${CAF_ALL_UNIT_TESTS} ${tests})
  endmacro()
else()
  macro(add_unit_tests globstr)
    # do nothing (unit tests disabled)
  endmacro()
endif()

# -- add targets ---------------------------------------------------------------

include_directories("${CAF_INCLUDE_DIRS}")

macro(add_caf_lib name header_only)
  string(TOUPPER ${name} upper_name)
  set(full_name "libcaf_${name}")
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${full_name}")
    message(STATUS "skip ${full_name} (not found)")
  else()
    add_subdirectory(${full_name})
    if(NOT ${header_only})
      set(shared_target ${full_name}_shared)
      set(static_target ${full_name}_static)
      set(lib_varname CAF_LIBRARY_${upper_name})
      set(lib_varname_static ${lib_varname}_STATIC)
      if(NOT CAF_BUILD_STATIC_ONLY)
        set(${lib_varname} ${shared_target})
        set(CAF_LIBRARIES ${CAF_LIBRARIES} ${shared_target})
      else()
        set(${lib_varname} ${static_target})
        set(CAF_LIBRARIES ${CAF_LIBRARIES} ${static_target})
      endif()
      if(CAF_BUILD_STATIC_ONLY OR CAF_BUILD_STATIC)
        set(${lib_varname_static} ${static_target})
      endif()
    endif()
    add_unit_tests("${full_name}/test/*.cpp")
    # add headers to include directories so other subprojects can use them
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/libcaf_${name}")
  endif()
endmacro()

add_caf_lib(bb yes)
add_caf_lib(net no)

# -- unit tests setup ----------------------------------------------------------

if(NOT CAF_NO_UNIT_TESTS)
  # setup unit test binary
  add_executable(incubator-test
                 "cmake/incubator-test.cpp"
                 ${CAF_ALL_UNIT_TESTS})
  target_link_libraries(incubator-test
                        ${CAF_EXTRA_LDFLAGS}
                        ${CAF_LIBRARIES}
                        ${PTHREAD_LIBRARIES})
  add_custom_target(all_unit_tests)
  add_dependencies(incubator-test all_unit_tests)
  # enumerate all test suites.
  foreach(test ${CAF_ALL_UNIT_TESTS})
    file(STRINGS ${test} contents)
    foreach(line ${contents})
      if ("${line}" MATCHES "CAF_SUITE (.*)")
        string(REGEX REPLACE ".* CAF_SUITE (.*)" "\\1" suite ${line})
        list(APPEND suites ${suite})
      endif()
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES suites)
  # creates one CMake test per test suite.
  macro (make_test suite)
    string(REPLACE " " "_" test_name ${suite})
    set(caf_test ${EXECUTABLE_OUTPUT_PATH}/incubator-test)
    add_test(${test_name} ${caf_test} -r 300 -n -v 5 -s "^${suite}$" ${ARGN})
  endmacro ()
  list(LENGTH suites num_suites)
  message(STATUS "Found ${num_suites} test suites")
  foreach(suite ${suites})
    make_test("${suite}")
  endforeach ()
endif()

# -- print summary -------------------------------------------------------------

# Inverts a boolean.
macro(invertYesNo in out)
  if(${in})
    set(${out} no)
  else()
    set(${out} yes)
  endif()
endmacro()

# Invert CAF_NO_* variables for nicer output.
invertYesNo(CAF_NO_UNIT_TESTS CAF_BUILD_UNIT_TESTS)

# Collect all compiler flags.
string(TOUPPER "${CMAKE_BUILD_TYPE}" UPPER_BUILD_TYPE)
set(ALL_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${UPPER_BUILD_TYPE}}")
set(ALL_LD_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${CAF_EXTRA_LDFLAGS}")
string(STRIP "${ALL_LD_FLAGS}" ALL_LD_FLAGS)

# Print summary.
if(NOT CAF_NO_SUMMARY)
  message(STATUS
        "\n====================|  Build Summary  |===================="
        "\n"
        "\nBuild type:            ${CMAKE_BUILD_TYPE}"
        "\nBuild static:          ${CAF_BUILD_STATIC}"
        "\nBuild static only:     ${CAF_BUILD_STATIC_ONLY}"
        "\nBuild static runtime:  ${CAF_BUILD_STATIC_RUNTIME}"
        "\n"
        "\nCXX:                   ${CMAKE_CXX_COMPILER}"
        "\nCXXFLAGS:              ${ALL_CXX_FLAGS}"
        "\nLINKER_FLAGS (shared): ${ALL_LD_FLAGS}"
        "\n"
        "\nSource directory:      ${CMAKE_CURRENT_SOURCE_DIR}"
        "\nBuild directory:       ${CMAKE_CURRENT_BINARY_DIR}"
        "\nExecutable path:       ${EXECUTABLE_OUTPUT_PATH}"
        "\nLibrary path:          ${LIBRARY_OUTPUT_PATH}"
        "\nInstall prefix:        ${CMAKE_INSTALL_PREFIX}"
        "\nGenerator:             ${CMAKE_GENERATOR}"
        "\n"
        "\n===========================================================\n")
endif()
