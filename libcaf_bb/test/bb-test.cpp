#define CAF_TEST_NO_MAIN

#include "bb-test.hpp"

#include "caf/init_global_meta_objects.hpp"
#include "caf/test/unit_test_impl.hpp"

int main(int argc, char** argv) {
  using namespace caf;
  init_global_meta_objects<id_block::bb_test>();
  core::init_global_meta_objects();
  return test::main(argc, argv);
}
