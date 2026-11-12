// Shmoo writers (design doc §6.5): CSV for baselines, ASCII for terminals and PRs, and a
// native SVG with every grade's spec box overlaid.

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <utility>

#include "../src/margin.hpp"
#include "../src/shmoo.hpp"

namespace i8086::testprog {
namespace {

std::string mhz_label(double mhz) { return std::format("{:g}", mhz); }

std::string grade_boxes(const ShmooResult& r) {
  std::string out;
  for (const auto& b : r.boxes) {
    out += std::format("{}{} <= {:g} MHz", out.empty() ? "" : ", ", spec::to_string(b.grade), b.fmax_mhz);
  }
  return out;
}

std::string escape(std::string_view s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

}  // namespace

void write_shmoo_csv(std::ostream& os, const ShmooResult& r) {
  os << "vcc_v,mhz,tclcl_ns,result,first_fail\n";
  for (std::size_t i = r.vcc_v.size(); i-- > 0;) {
    for (std::size_t j = 0; j < r.mhz.size(); ++j) {
      const ShmooCell& c = r.at(i, j);
      os << std::format("{:.2f},{},{:.1f},{},{}\n", c.vcc_v, mhz_label(c.mhz), 1000.0 / c.mhz,
                        c.pass ? "pass" : "fail", c.failed_rom);
    }
  }
}

void write_shmoo_ascii(std::ostream& os, const ShmooResult& r) {
  const auto widest = std::ranges::max_element(r.boxes, {}, &ShmooBox::fmax_mhz);
  os << std::format("Shmoo: die {}, TA {:g} C, functional suite ({} ROMs per cell)\n", r.die, r.temp_c,
                    functional_suite().size());
  os << "VCC\\MHz";
  for (double f : r.mhz) os << std::format("{:>5}", mhz_label(f));
  os << "\n";
  for (std::size_t i = r.vcc_v.size(); i-- > 0;) {
    os << std::format(" {:<6.2f}", r.vcc_v[i]);
    for (std::size_t j = 0; j < r.mhz.size(); ++j) {
      const ShmooCell& c = r.at(i, j);
      const bool boxed = widest != r.boxes.end() && r.in_box(c, *widest);
      const char mark = c.pass ? '*' : (r.in_any_box(c) ? 'X' : '.');
      os << (boxed ? std::format("  [{}]", mark) : std::format("   {} ", mark));
    }
    os << "\n";
  }
  os << std::format("* pass  . fail  X fail inside a spec box  [ ] = {} spec box ({:g}-{:g} V)\n",
                    widest != r.boxes.end() ? spec::to_string(widest->grade) : "", kVccOperatingMin,
                    kVccOperatingMax);
  os << "spec boxes: " << grade_boxes(r) << "; " << r.box_fails() << " failing cell(s) inside a box\n";
}

void write_shmoo_svg(std::ostream& os, const ShmooResult& r) {
  constexpr int cw = 44, ch = 26, left = 70, top = 56, right = 150, bottom = 60;
  const int cols = static_cast<int>(r.mhz.size()), rows = static_cast<int>(r.vcc_v.size());
  const int w = left + cols * cw + right, h = top + rows * ch + bottom;
  const auto x_of = [&](std::size_t j) { return left + static_cast<int>(j) * cw; };
  const auto y_of = [&](std::size_t i) { return top + (rows - 1 - static_cast<int>(i)) * ch; };

  os << std::format(R"(<svg xmlns="http://www.w3.org/2000/svg" width="{}" height="{}" viewBox="0 0 {} {}" )"
                    R"(font-family="Helvetica, Arial, sans-serif" font-size="12">)"
                    "\n",
                    w, h, w, h);
  os << std::format(R"(<rect width="{}" height="{}" fill="#ffffff"/>)" "\n", w, h);
  os << std::format(R"(<text x="{}" y="22" font-size="15" font-weight="bold">Shmoo: die {}, TA {:g} °C</text>)" "\n",
                    left, escape(r.die), r.temp_c);
  os << std::format(R"(<text x="{}" y="40" fill="#555">Functional suite, {} ROMs per cell; {} failing cell(s) inside a spec box</text>)" "\n",
                    left, functional_suite().size(), r.box_fails());

  for (std::size_t i = 0; i < r.vcc_v.size(); ++i) {
    for (std::size_t j = 0; j < r.mhz.size(); ++j) {
      const ShmooCell& c = r.at(i, j);
      const char* fill = c.pass ? "#4caf50" : (r.in_any_box(c) ? "#b71c1c" : "#ef9a9a");
      os << std::format(R"(<rect x="{}" y="{}" width="{}" height="{}" fill="{}" stroke="#ffffff">)", x_of(j), y_of(i),
                        cw, ch, fill);
      os << std::format("<title>{:.2f} V, {} MHz: {}{}</title></rect>\n", c.vcc_v, mhz_label(c.mhz),
                        c.pass ? "pass" : "fail", c.pass ? "" : " (" + escape(c.why) + ")");
    }
  }
  for (std::size_t j = 0; j < r.mhz.size(); ++j) {
    os << std::format(R"(<text x="{}" y="{}" text-anchor="middle">{}</text>)" "\n", x_of(j) + cw / 2,
                      top + rows * ch + 16, mhz_label(r.mhz[j]));
  }
  for (std::size_t i = 0; i < r.vcc_v.size(); ++i) {
    os << std::format(R"(<text x="{}" y="{}" text-anchor="end">{:.2f}</text>)" "\n", left - 8, y_of(i) + ch / 2 + 4,
                      r.vcc_v[i]);
  }
  os << std::format(R"(<text x="{}" y="{}" text-anchor="middle">Frequency (MHz)</text>)" "\n", left + cols * cw / 2,
                    top + rows * ch + 40);
  os << std::format(R"svg(<text x="18" y="{}" text-anchor="middle" transform="rotate(-90 18 {})">VCC (V)</text>)svg" "\n",
                    top + rows * ch / 2, top + rows * ch / 2);

  const char* colors[] = {"#0d47a1", "#6a1b9a", "#e65100"};
  for (std::size_t b = 0; b < r.boxes.size(); ++b) {
    const ShmooBox& box = r.boxes[b];
    std::optional<std::size_t> j_last, i_lo, i_hi;
    for (std::size_t j = 0; j < r.mhz.size(); ++j) {
      if (r.mhz[j] <= box.fmax_mhz + 1e-9) j_last = j;
    }
    for (std::size_t i = 0; i < r.vcc_v.size(); ++i) {
      if (r.vcc_v[i] < kVccOperatingMin - 1e-9 || r.vcc_v[i] > kVccOperatingMax + 1e-9) continue;
      if (!i_lo) i_lo = i;
      i_hi = i;
    }
    if (!j_last || !i_lo) continue;
    const int x0 = x_of(0), x1 = x_of(*j_last) + cw, y0 = y_of(*i_hi), y1 = y_of(*i_lo) + ch;
    const char* color = colors[b % 3];
    os << std::format(R"(<rect x="{}" y="{}" width="{}" height="{}" fill="none" stroke="{}" stroke-width="2.5" stroke-dasharray="6 3"/>)" "\n",
                      x0 + 1 + static_cast<int>(b), y0 + 1 + static_cast<int>(b), x1 - x0 - 2 - 2 * static_cast<int>(b),
                      y1 - y0 - 2 - 2 * static_cast<int>(b), color);
    os << std::format(R"(<text x="{}" y="{}" fill="{}" font-weight="bold">{} ≤ {:g} MHz</text>)" "\n",
                      left + cols * cw + 12, top + 16 + 18 * static_cast<int>(b), color, spec::to_string(box.grade),
                      box.fmax_mhz);
  }
  const int ly = top + 16 + 18 * static_cast<int>(r.boxes.size()) + 12;
  const std::pair<const char*, const char*> legend[] = {
      {"#4caf50", "pass"}, {"#ef9a9a", "fail"}, {"#b71c1c", "fail in spec box"}};
  for (int k = 0; k < 3; ++k) {
    os << std::format(R"(<rect x="{}" y="{}" width="12" height="12" fill="{}"/><text x="{}" y="{}">{}</text>)" "\n",
                      left + cols * cw + 12, ly + 18 * k, legend[k].first, left + cols * cw + 30, ly + 18 * k + 10,
                      legend[k].second);
  }
  os << "</svg>\n";
}

}  // namespace i8086::testprog
