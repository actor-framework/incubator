#pragma once

#include <vector>

#include "caf/fwd.hpp"
#include "caf/stream.hpp"
#include "caf/test/dsl.hpp"
#include "caf/test/unit_test_impl.hpp"
#include "caf/type_id.hpp"

CAF_BEGIN_TYPE_ID_BLOCK(incubator_test, caf::first_custom_type_id)

CAF_ADD_TYPE_ID(incubator_test, (caf::stream<int>) )
CAF_ADD_TYPE_ID(incubator_test, (std::vector<int>) )

CAF_END_TYPE_ID_BLOCK(incubator_test)
