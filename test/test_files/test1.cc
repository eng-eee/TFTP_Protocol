// #include "gtest/gtest.h"
#include <gtest/gtest.h>
#include <thread>

// Basit test
TEST(MathTest, Addition) {
    EXPECT_EQ(2 + 3, 5);
    std::this_thread::sleep_for(std::chrono::seconds(1));
}