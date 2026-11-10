#pragma once

// Layer 4 — Test program. FROZEN INTERFACE (M0): changes need review from the
// Spec & Integration lead and every affected layer owner (see CODEOWNERS).
// Test cases reach the device only through tester::Tester.

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "i8086/golden/emulator.hpp"
#include "i8086/spec/spec.hpp"
#include "i8086/testprog/datalog.hpp"
#include "i8086/tester/tester.hpp"

namespace i8086::testprog {

struct TestContext {
  tester::Tester& t;
  const spec::Spec& spec;
  spec::Grade grade;
  double vcc_v;
  double temp_c;
  Datalog& log;
  const golden::Emulator* golden;
};

class TestCase {
 public:
  virtual ~TestCase() = default;
  virtual std::string_view id() const = 0;  // brief §7 ID, e.g. "B3"
  virtual std::string_view title() const = 0;
  virtual std::vector<std::string_view> spec_symbols() const { return {}; }  // limits it judges
  virtual Verdict run(TestContext&) = 0;
};

class TestRegistry {
 public:
  using Factory = std::function<std::unique_ptr<TestCase>()>;

  static TestRegistry& global();

  bool add(Factory);  // throws std::logic_error on a duplicate id
  std::vector<std::string> ids() const;  // brief order: A1, A2, ..., A10, B1, ...
  std::unique_ptr<TestCase> create(std::string_view id) const;
  // Comma-separated: "*", "A*", "D1-D11", "B3". Throws std::invalid_argument on an unknown exact id.
  std::vector<std::string> select(std::string_view selector) const;

 private:
  std::vector<std::pair<std::string, Factory>> tests_;
};

}  // namespace i8086::testprog

#define I86_TESTPROG_CAT2(a, b) a##b
#define I86_TESTPROG_CAT(a, b) I86_TESTPROG_CAT2(a, b)
// Registers a TestCase subclass into TestRegistry::global() at static-init time.
// Case sources must be linked as object files (see testprog/CMakeLists.txt).
#define REGISTER_TEST(cls)                                                             \
  static const bool I86_TESTPROG_CAT(i86_registered_, cls) =                           \
      ::i8086::testprog::TestRegistry::global().add(                                   \
          [] { return std::unique_ptr<::i8086::testprog::TestCase>(new cls()); })
