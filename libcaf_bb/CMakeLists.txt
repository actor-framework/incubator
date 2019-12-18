# -- get header files for creating "proper" XCode projects ---------------------

file(GLOB_RECURSE CAF_BB_HEADERS "caf/*.hpp")

# -- list cpp files for caf::bb ------------------------------------------------

set(CAF_BB_SOURCES
  # nop
)

# -- list cpp files for caf-bb-test --------------------------------------------

set(CAF_BB_TEST_SOURCES
  test/container_source.cpp
  test/stream_reader.cpp
  test/tokenized_integer_reader.cpp
)

# -- add library target --------------------------------------------------------

# --> Uncomment this block when adding the first .cpp file
# 
# add_library(libcaf_bb_obj OBJECT ${CAF_BB_SOURCES} ${CAF_BB_HEADERS})
# 
# add_library(libcaf_bb $<TARGET_OBJECTS:libcaf_bb_obj>)
# 
# add_library(caf::bb ALIAS libcaf_bb)
# 
# if(BUILD_SHARED_LIBS AND NOT WIN32)
#   target_compile_options(libcaf_bb PRIVATE -fPIC)
#   target_compile_options(libcaf_bb_obj PRIVATE -fPIC)
# endif()
# 
# target_link_libraries(libcaf_bb PUBLIC ${CAF_EXTRA_LDFLAGS} ${CAF_LIBRARIES})
# 
# generate_export_header(libcaf_bb
#                        EXPORT_MACRO_NAME CAF_BB_EXPORT
#                        EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/caf/detail/bb_export.hpp"
#                        STATIC_DEFINE CAF_STATIC_BUILD)
# 
# target_compile_definitions(libcaf_bb_obj PRIVATE libcaf_bb_EXPORTS)
# 
# set_target_properties(libcaf_bb PROPERTIES
#                       EXPORT_NAME bb
#                       SOVERSION ${CAF_VERSION}
#                       VERSION ${CAF_LIB_VERSION}
#                       OUTPUT_NAME caf_bb)
#
# -- install library and header files ------------------------------------------
#
# install(FILES "${CMAKE_BINARY_DIR}/caf/detail/bb_export.hpp"
#         DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/caf/detail")
#
# install(TARGETS libcaf_bb
#         EXPORT CAFTargets
#         ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT bb
#         RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT bb
#         LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT bb)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/caf"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT bb
        FILES_MATCHING PATTERN "*.hpp")

# -- build unit tests ----------------------------------------------------------

if(NOT CAF_NO_UNIT_TESTS) 
  message(STATUS "${CMAKE_CURRENT_SOURCE_DIR}")
  add_executable(caf-bb-test
                 "${PROJECT_SOURCE_DIR}/cmake/incubator-test.cpp"
                 "${CAF_INCLUDE_DIR_TEST}/caf/test/unit_test.hpp"
                 "${CAF_INCLUDE_DIR_TEST}/caf/test/unit_test_impl.hpp"
                 ${CAF_BB_TEST_SOURCES})
# --> Remove this when adding the first cpp 
  target_include_directories(caf-bb-test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
#                  $<TARGET_OBJECTS:libcaf_bb_obj>)
#   target_compile_definitions(caf-bb-test PRIVATE libcaf_bb_EXPORTS)
  target_link_libraries(caf-bb-test ${CAF_EXTRA_LDFLAGS} ${CAF_LIBRARIES})
  add_test_suites(caf-bb-test
                  "${CMAKE_CURRENT_SOURCE_DIR}"
                  ${CAF_BB_TEST_SOURCES})
endif()

# -- add this library to the global CAF_LIBRARIES ------------------------------

list(APPEND CAF_LIBRARIES libcaf_bb)

set(CAF_LIBRARIES ${CAF_LIBRARIES} PARENT_SCOPE)
