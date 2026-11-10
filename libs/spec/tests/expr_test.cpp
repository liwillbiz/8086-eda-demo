#include <gtest/gtest.h>

#include "expr.hpp"

namespace i8086::spec::detail {
namespace {

double eval(std::string_view text, double tclcl = 0) {
  return Expr::parse(text).eval([&](const Var& v) {
    if (v.name == "TCLCL" && !v.bound) return tclcl;
    if (v.name == "X" && v.bound == Bound::Min) return 7.0;
    if (v.name == "X" && v.bound == Bound::Max) return 9.0;
    throw std::runtime_error("unexpected var " + v.name);
  });
}

TEST(Expr, Constants) {
  EXPECT_DOUBLE_EQ(eval("10"), 10);
  EXPECT_DOUBLE_EQ(eval("-0.5"), -0.5);
  EXPECT_DOUBLE_EQ(eval(".5"), 0.5);
  EXPECT_DOUBLE_EQ(eval("1e3"), 1000);
  EXPECT_TRUE(Expr::parse("2*3").is_constant());
}

TEST(Expr, PrecedenceAndAssociativity) {
  EXPECT_DOUBLE_EQ(eval("2+3*4"), 14);
  EXPECT_DOUBLE_EQ(eval("(2+3)*4"), 20);
  EXPECT_DOUBLE_EQ(eval("10-4-3"), 3);
  EXPECT_DOUBLE_EQ(eval("24/4/2"), 3);
  EXPECT_DOUBLE_EQ(eval("-2*-3"), 6);
  EXPECT_DOUBLE_EQ(eval(" 1 - - 1 "), 2);
}

TEST(Expr, DatasheetFormulas) {
  EXPECT_DOUBLE_EQ(eval("2*TCLCL-75", 200), 325);
  EXPECT_NEAR(eval("2/3*TCLCL-15", 200), 118.333, 1e-3);
  EXPECT_NEAR(eval("(1/3*TCLCL)+2", 125), 43.667, 1e-3);
  EXPECT_DOUBLE_EQ(eval("X.min-20"), -13);
  EXPECT_DOUBLE_EQ(eval("X.max"), 9);
}

TEST(Expr, ReportsVariables) {
  const Expr e = Expr::parse("TCLCH.min - 10 + TCLCL");
  ASSERT_EQ(e.vars().size(), 2u);
  EXPECT_EQ(e.vars()[0], (Var{"TCLCH", Bound::Min}));
  EXPECT_EQ(e.vars()[1], (Var{"TCLCL", std::nullopt}));
  EXPECT_FALSE(e.is_constant());
  EXPECT_EQ(e.text(), "TCLCH.min - 10 + TCLCL");
}

std::size_t error_column(std::string_view text) {
  try {
    Expr::parse(text);
  } catch (const ExprError& e) {
    return e.column();
  }
  ADD_FAILURE() << "expected ExprError for '" << text << "'";
  return 0;
}

TEST(Expr, SyntaxErrorsCarryColumn) {
  EXPECT_EQ(error_column(""), 0u);
  EXPECT_EQ(error_column("2*TCLCL-"), 8u);
  EXPECT_EQ(error_column("2**3"), 2u);
  EXPECT_EQ(error_column("(1+2"), 4u);
  EXPECT_EQ(error_column("1+2)"), 3u);
  EXPECT_EQ(error_column("TCLCH.typ"), 6u);
  EXPECT_EQ(error_column("1.2.3"), 0u);
  EXPECT_EQ(error_column("3 $"), 2u);
}

TEST(Expr, DivisionByZero) { EXPECT_THROW(eval("1/(TCLCL-100)", 100), ExprError); }

}  // namespace
}  // namespace i8086::spec::detail
