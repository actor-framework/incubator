#define CAF_TEST_NO_MAIN

#include "caf/net/test/incubator_test.hpp"
#include "caf/test/unit_test_impl.hpp"

int main(int argc, char** argv) {
  caf::init_global_meta_objects<>();
  return caf::test::main(argc, argv);
}