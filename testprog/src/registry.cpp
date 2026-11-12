#include <algorithm>
#include <charconv>
#include <stdexcept>

#include "i8086/testprog/test_case.hpp"

namespace i8086::testprog {
namespace {

struct IdKey {
  std::string group;
  int num = 0;
};

IdKey key_of(std::string_view id) {
  IdKey k;
  size_t i = 0;
  while (i < id.size() && (id[i] < '0' || id[i] > '9')) ++i;
  k.group = std::string(id.substr(0, i));
  std::from_chars(id.data() + i, id.data() + id.size(), k.num);
  return k;
}

bool brief_order(std::string_view a, std::string_view b) {
  const IdKey ka = key_of(a), kb = key_of(b);
  if (ka.group != kb.group) return ka.group < kb.group;
  return ka.num < kb.num;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
  return s;
}

}  // namespace

TestRegistry& TestRegistry::global() {
  static TestRegistry registry;
  return registry;
}

bool TestRegistry::add(Factory factory) {
  std::string id(factory()->id());
  for (const auto& [existing, _] : tests_) {
    if (existing == id) throw std::logic_error("duplicate test id " + id);
  }
  tests_.emplace_back(std::move(id), std::move(factory));
  return true;
}

std::vector<std::string> TestRegistry::ids() const {
  std::vector<std::string> out;
  for (const auto& [id, _] : tests_) out.push_back(id);
  std::ranges::sort(out, brief_order);
  return out;
}

std::unique_ptr<TestCase> TestRegistry::create(std::string_view id) const {
  for (const auto& [existing, factory] : tests_) {
    if (existing == id) return factory();
  }
  throw std::invalid_argument("unknown test id " + std::string(id));
}

std::vector<std::string> TestRegistry::select(std::string_view selector) const {
  const auto all = ids();
  std::vector<std::string> out;
  auto take = [&](const std::string& id) {
    if (std::ranges::find(out, id) == out.end()) out.push_back(id);
  };

  while (!selector.empty()) {
    const size_t comma = selector.find(',');
    const std::string_view tok = trim(selector.substr(0, comma));
    selector = comma == std::string_view::npos ? std::string_view{} : selector.substr(comma + 1);
    if (tok.empty()) continue;

    if (tok.back() == '*') {
      const std::string_view prefix = tok.substr(0, tok.size() - 1);
      for (const auto& id : all) {
        if (std::string_view(id).starts_with(prefix)) take(id);
      }
    } else if (const size_t dash = tok.find('-'); dash != std::string_view::npos) {
      const IdKey lo = key_of(tok.substr(0, dash)), hi = key_of(tok.substr(dash + 1));
      if (lo.group != hi.group) throw std::invalid_argument("range spans groups: " + std::string(tok));
      for (const auto& id : all) {
        const IdKey k = key_of(id);
        if (k.group == lo.group && k.num >= lo.num && k.num <= hi.num) take(id);
      }
    } else {
      if (std::ranges::find(all, tok) == all.end()) {
        throw std::invalid_argument("unknown test id " + std::string(tok));
      }
      take(std::string(tok));
    }
  }
  std::ranges::sort(out, brief_order);
  return out;
}

}  // namespace i8086::testprog
