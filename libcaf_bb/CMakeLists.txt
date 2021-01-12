# -- get header files for creating "proper" XCode projects ---------------------

file(GLOB_RECURSE CAF_BB_HEADERS "caf/*.hpp")

# -- add targets ---------------------------------------------------------------

caf_incubator_add_component(
  bb
  DEPENDENCIES
    INTERFACE
      CAF::core
  HEADERS
    ${CAF_BB_HEADERS}
  TEST_SOURCES
    test/bb-test.cpp
  TEST_SUITES
    container_source
    stream_reader
    tokenized_integer_reader)

# CAF::bb is header-only, so it won't pull in the dependencies for the tests.
if(TARGET caf-bb-test)
  target_link_libraries(caf-bb-test PRIVATE CAF::core CAF::internal)
endif()
