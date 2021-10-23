#ifndef __cpp_noexcept_function_type
#  error "Noexcept not part of the type system (__cpp_noexcept_function_type)"
#endif

#ifndef __cpp_fold_expressions
#  error "No support for fold expression (__cpp_fold_expressions)"
#endif

#ifndef __cpp_if_constexpr
#  error "No support for 'if constexpr' (__cpp_if_constexpr)"
#endif

#include <vector>

int main(int, char**) {
  std::vector xs{21};
  xs.emplace_back(42);
  return 0;
}
