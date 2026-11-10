#include "i8086/tester/pattern.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace i8086::tester {
namespace {

constexpr std::string_view kVectorChars = "01ZLHXMC";
const std::set<std::string, std::less<>> kGlobalAttrs = {"period", "clk_high", "drive", "strobe",
                                                         "fmt", "grade"};
const std::set<std::string, std::less<>> kPinAttrs = {"drive", "strobe", "fmt"};

class ParseError : public std::runtime_error {
 public:
  ParseError(std::string_view origin, int line, const std::string& msg)
      : std::runtime_error(std::string(origin) + ":" + std::to_string(line) + ": " + msg) {}
};

std::vector<std::string> split_ws(std::string_view s) {
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    const std::size_t start = i;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i > start) out.emplace_back(s.substr(start, i - start));
  }
  return out;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

template <class T>
bool parse_uint(std::string_view s, T& out) {
  const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
  return ec == std::errc{} && end == s.data() + s.size();
}

// "AD[15:0]" -> AD15 ... AD0; "A[16:19]" ascends. Anything else is one pin.
std::vector<std::string> expand_pins(const std::string& tok, const auto& fail) {
  const auto open = tok.find('[');
  if (open == std::string::npos) return {tok};
  const auto colon = tok.find(':', open);
  if (colon == std::string::npos || tok.back() != ']' || open == 0) fail("bad bus syntax '" + tok + "'");
  int hi = 0, lo = 0;
  if (!parse_uint(std::string_view(tok).substr(open + 1, colon - open - 1), hi) ||
      !parse_uint(std::string_view(tok).substr(colon + 1, tok.size() - colon - 2), lo)) {
    fail("bad bus range '" + tok + "'");
  }
  const std::string base = tok.substr(0, open);
  std::vector<std::string> out;
  const int step = hi >= lo ? -1 : 1;
  for (int i = hi;; i += step) {
    out.push_back(base + std::to_string(i));
    if (i == lo) break;
  }
  return out;
}

struct Group {
  std::string text;
  std::size_t width;
};

// Inverse of expand_pins: runs of PREFIXn with consecutive indices become PREFIX[a:b].
std::vector<Group> group_pins(const std::vector<std::string>& pins) {
  auto split_name = [](const std::string& p, std::string& prefix, int& idx) {
    std::size_t d = p.size();
    while (d > 0 && std::isdigit(static_cast<unsigned char>(p[d - 1]))) --d;
    if (d == 0 || d == p.size() || (p.size() - d > 1 && p[d] == '0')) return false;
    prefix = p.substr(0, d);
    return parse_uint(std::string_view(p).substr(d), idx);
  };
  std::vector<Group> out;
  std::size_t i = 0;
  while (i < pins.size()) {
    std::string prefix;
    int first = 0;
    std::size_t j = i + 1;
    int step = 0;
    if (split_name(pins[i], prefix, first)) {
      int prev = first;
      for (; j < pins.size(); ++j) {
        std::string p2;
        int idx = 0;
        if (!split_name(pins[j], p2, idx) || p2 != prefix) break;
        const int d = idx - prev;
        if ((d != 1 && d != -1) || (step != 0 && d != step)) break;
        step = d;
        prev = idx;
      }
      if (j - i >= 2) {
        out.push_back({prefix + "[" + std::to_string(first) + ":" + std::to_string(prev) + "]", j - i});
        i = j;
        continue;
      }
    }
    out.push_back({pins[i], 1});
    ++i;
  }
  return out;
}

}  // namespace

Pattern Pattern::parse(std::string_view text, std::string_view origin) {
  Pattern pat;
  bool have_pattern = false, have_pins = false;
  std::set<int> timing_ids;
  int line_no = 0;
  std::size_t pos = 0;
  std::vector<std::pair<int, std::string>> pin_attr_refs;

  while (pos <= text.size()) {
    const std::size_t nl = text.find('\n', pos);
    std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
    ++line_no;
    const auto fail = [&](const std::string& msg) { throw ParseError(origin, line_no, msg); };

    std::string comment;
    if (const auto hash = line.find('#'); hash != std::string_view::npos) {
      comment = std::string(trim(line.substr(hash + 1)));
      line = line.substr(0, hash);
    }
    const auto tok = split_ws(line);
    if (tok.empty()) continue;
    const std::string& kw = tok[0];

    if (kw == "pattern") {
      if (have_pattern) fail("duplicate 'pattern' line");
      if (tok.size() != 2) fail("expected: pattern NAME");
      pat.name = tok[1];
      have_pattern = true;
    } else if (kw == "spec") {
      if (tok.size() != 2) fail("expected: spec PATH");
      pat.spec_ref = tok[1];
    } else if (kw == "pins") {
      if (have_pins) fail("duplicate 'pins' line");
      if (tok.size() < 2) fail("'pins' needs at least one pin");
      std::set<std::string, std::less<>> seen;
      for (std::size_t i = 1; i < tok.size(); ++i) {
        for (auto& p : expand_pins(tok[i], fail)) {
          if (!seen.insert(p).second) fail("duplicate pin '" + p + "'");
          pat.pins.push_back(std::move(p));
        }
      }
      have_pins = true;
    } else if (kw == "timing") {
      PatternTiming t;
      if (tok.size() < 2 || !parse_uint(tok[1], t.id)) fail("expected: timing ID key=value ...");
      if (!timing_ids.insert(t.id).second) fail("duplicate timing " + tok[1]);
      for (std::size_t i = 2; i < tok.size(); ++i) {
        const auto eq = tok[i].find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 == tok[i].size()) {
          fail("expected key=value, got '" + tok[i] + "'");
        }
        std::string key = tok[i].substr(0, eq);
        if (const auto dot = key.find('.'); dot != std::string::npos) {
          if (!kPinAttrs.contains(std::string_view(key).substr(0, dot))) {
            fail("unknown per-pin timing attribute '" + key + "'");
          }
          pin_attr_refs.emplace_back(line_no, key.substr(dot + 1));
        } else if (!kGlobalAttrs.contains(key)) {
          fail("unknown timing attribute '" + key + "'");
        }
        if (!t.attrs.emplace(key, tok[i].substr(eq + 1)).second) fail("duplicate attribute '" + key + "'");
      }
      pat.timings.push_back(std::move(t));
    } else if (kw == "v") {
      if (!have_pins) fail("vector before 'pins' line");
      Vector v;
      if (tok.size() < 2 || !parse_uint(tok[1], v.timing_id)) fail("expected: v TIMING CHARS ...");
      std::size_t i = 2;
      for (; i < tok.size() && tok[i] != "repeat"; ++i) v.chars += tok[i];
      if (i < tok.size()) {
        if (i + 2 != tok.size() || !parse_uint(tok[i + 1], v.repeat) || v.repeat == 0) {
          fail("expected: repeat N (N >= 1) at the end of the vector");
        }
      }
      if (v.chars.size() != pat.pins.size()) {
        fail("vector has " + std::to_string(v.chars.size()) + " pin chars, 'pins' declares " +
             std::to_string(pat.pins.size()));
      }
      for (char c : v.chars) {
        if (kVectorChars.find(c) == std::string_view::npos) {
          fail(std::string("bad vector char '") + c + "' (allowed: 0 1 Z L H X M C)");
        }
      }
      v.comment = std::move(comment);
      pat.vectors.push_back(std::move(v));
    } else {
      fail("unknown keyword '" + kw + "'");
    }
  }

  if (!have_pattern) throw ParseError(origin, line_no, "missing 'pattern NAME' line");
  if (!have_pins) throw ParseError(origin, line_no, "missing 'pins' line");
  for (const auto& [ln, pin] : pin_attr_refs) {
    if (std::ranges::find(pat.pins, pin) == pat.pins.end()) {
      throw ParseError(origin, ln, "timing attribute names pin '" + pin + "' not in 'pins'");
    }
  }
  return pat;
}

Pattern Pattern::load(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open pattern " + path.string());
  std::stringstream ss;
  ss << in.rdbuf();
  return parse(ss.str(), path.string());
}

std::string Pattern::to_text() const {
  std::ostringstream os;
  os << "pattern " << name << "\n";
  if (!spec_ref.empty()) os << "spec    " << spec_ref << "\n";
  const auto groups = group_pins(pins);
  os << "pins   ";
  for (const auto& g : groups) os << " " << g.text;
  os << "\n";
  for (const auto& t : timings) {
    os << "timing  " << t.id;
    for (const auto& [k, v] : t.attrs) os << " " << k << "=" << v;
    os << "\n";
  }
  for (const auto& v : vectors) {
    os << "v " << v.timing_id;
    std::size_t at = 0;
    for (const auto& g : groups) {
      os << " " << std::string_view(v.chars).substr(at, g.width);
      at += g.width;
    }
    if (v.repeat != 1) os << " repeat " << v.repeat;
    if (!v.comment.empty()) os << "  # " << v.comment;
    os << "\n";
  }
  return os.str();
}

}  // namespace i8086::tester
