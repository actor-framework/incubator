# -- get header files for creating "proper" XCode projects ---------------------

file(GLOB_RECURSE CAF_BB_HEADERS "caf/*.hpp")

# -- list cpp files for caf::bb ------------------------------------------------

add_library(libcaf_bb INTERFACE)

target_link_libraries(libcaf_bb INTERFACE CAF::core)

# -- install library and header files ------------------------------------------

# install(FILES "${CMAKE_BINARY_DIR}/caf/detail/bb_export.hpp"
#         DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/caf/detail")

install(TARGETS libcaf_bb
        EXPORT CAFTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT bb
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT bb
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT bb)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/caf"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT bb
        FILES_MATCHING PATTERN "*.hpp")

# -- build unit tests ----------------------------------------------------------

if(NOT CAF_INC_ENABLE_TESTING)
  return()
endif()

add_executable(caf-bb-test test/bb-test.cpp)

target_include_directories(caf-bb-test PRIVATE
                           "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${CMAKE_CURRENT_SOURCE_DIR}/test")

target_compile_definitions(caf-bb-test PRIVATE libcaf_bb_EXPORTS)

target_link_libraries(caf-bb-test PRIVATE CAF::core CAF::test)

caf_incubator_add_test_suites(caf-bb-test
  container_source
  stream_reader
  tokenized_integer_reader
)
