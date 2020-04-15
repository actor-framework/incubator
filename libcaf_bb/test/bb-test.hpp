#include "caf/test/dsl.hpp"

using i32_vector = std::vector<int32_t>;

CAF_BEGIN_TYPE_ID_BLOCK(bb_test, caf::first_custom_type_id)

  CAF_ADD_TYPE_ID(bb_test, (caf::stream<int32_t>) )
  CAF_ADD_TYPE_ID(bb_test, (std::vector<int32_t>) )

CAF_END_TYPE_ID_BLOCK(bb_test)
