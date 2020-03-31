#define CAF_TEST_NO_MAIN

#include "incubator-test.hpp"

int main(int argc, char** argv) {
  caf::core::init_global_meta_objects();
  caf::init_global_meta_objects<caf::id_block::incubator_test>();
  return caf::test::main(argc, argv);
}
