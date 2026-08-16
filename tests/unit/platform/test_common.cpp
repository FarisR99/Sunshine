/**
 * @file tests/unit/platform/test_common.cpp
 * @brief Test src/platform/common.*.
 */
#include "../../tests_common.h"

#include <boost/asio/ip/host_name.hpp>
#include <src/platform/common.h>

TEST(HostnameTests, TestAsioEquality) {
  // These should be equivalent on all platforms for ASCII hostnames
  ASSERT_EQ(platf::get_host_name(), boost::asio::ip::host_name());
}

TEST(AppdataOverride, RedirectsAndResets) {
  // Capture the platform default so we can verify a clean reset afterward.
  const auto original = platf::appdata();

  const std::filesystem::path redirected {"sunshine-appdata-override-test"};
  platf::set_appdata_dir(redirected);
  EXPECT_EQ(platf::appdata(), redirected);

  // Clearing the override restores the platform default exactly.
  platf::set_appdata_dir({});
  EXPECT_TRUE(platf::appdata_override().empty());
  EXPECT_EQ(platf::appdata(), original);
}
