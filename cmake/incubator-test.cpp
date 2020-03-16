#define CAF_TEST_NO_MAIN

#include "caf/fwd.hpp"
#include "caf/test/dsl.hpp"
#include "caf/test/unit_test_impl.hpp"
#include "caf/type_id.hpp"

int main(int argc, char** argv) {
  caf::init_global_meta_objects<>();
  return caf::test::main(argc, argv);
}
