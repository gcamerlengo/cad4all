// SPDX-License-Identifier: GPL-3.0-or-later
#include "coordinput.h"

#include <gtest/gtest.h>

#include <clocale>

using namespace cad;

TEST(CoordInput, Numbers)
{
    EXPECT_DOUBLE_EQ(parseNumber("12.5").value(), 12.5);
    EXPECT_DOUBLE_EQ(parseNumber(" -3 ").value(), -3.0);
    EXPECT_FALSE(parseNumber("12,5"));
    EXPECT_FALSE(parseNumber("L"));
    EXPECT_FALSE(parseNumber(""));
    EXPECT_FALSE(parseNumber("10abc"));
}

TEST(CoordInput, AbsoluteAndRelativePoints)
{
    EXPECT_EQ(parsePoint("10,20", std::nullopt), (Vec2{10, 20}));
    EXPECT_EQ(parsePoint("#10,20", Vec2{5, 5}), (Vec2{10, 20}));
    EXPECT_EQ(parsePoint("@5,-2", Vec2{1, 1}), (Vec2{6, -1}));
    EXPECT_EQ(parsePoint("@", Vec2{1, 1}), (Vec2{1, 1}));
    // Relative input needs a previous point.
    EXPECT_FALSE(parsePoint("@5,0", std::nullopt));
    EXPECT_FALSE(parsePoint("10", std::nullopt));
}

TEST(CoordInput, PolarPoints)
{
    const auto p = parsePoint("@10<90", Vec2{1, 1});
    ASSERT_TRUE(p);
    EXPECT_NEAR(p->x, 1.0, 1e-12);
    EXPECT_NEAR(p->y, 11.0, 1e-12);
    const auto q = parsePoint("2<180", std::nullopt);
    ASSERT_TRUE(q);
    EXPECT_NEAR(q->x, -2.0, 1e-12);
    // Quadrant angles are exact, not 6e-16 off.
    EXPECT_EQ(parsePoint("@1000<90", Vec2{0, 0})->x, 0.0);
    EXPECT_EQ(parsePoint("@1000<-90", Vec2{0, 0})->y, -1000.0);
    EXPECT_EQ(parsePoint("@1000<450", Vec2{0, 0})->x, 0.0);
}

TEST(CoordInput, IgnoresDecimalCommaLocale)
{
    // An Italian locale must not change how "1.5" is read.
    std::setlocale(LC_NUMERIC, "it_IT.UTF-8");
    EXPECT_DOUBLE_EQ(parseNumber("1.5").value(), 1.5);
    std::setlocale(LC_NUMERIC, "C");
}
