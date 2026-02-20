#include <catch2/catch_test_macros.hpp>
#include <string>

// Include loguru if available (header-only usage in tests)
#ifdef __has_include
#  if __has_include(<loguru/loguru.hpp>)
#    include <loguru/loguru.hpp>
#  endif
#endif

TEST_CASE("basic arithmetic") {
    REQUIRE(1 + 1 == 2);
}
