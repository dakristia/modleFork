#pragma once

#include <absl/types/span.h>
#include <fmt/format.h>

#include <algorithm>
#include <boost/math/distributions/beta.hpp>
#include <boost/math/distributions/students_t.hpp>
#include <boost/math/special_functions/erf.hpp>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

#include "./correlation_utils.hpp"
#include "modle/suppress_compiler_warnings.hpp"

namespace modle::correlation {

double compute_pearson_significance(double pcc, std::size_t n) {
  assert(n > 2);  // NOLINT
  const auto ab = static_cast<double>(n) / 2.0 - 1;
  boost::math::beta_distribution<double> dist(ab, ab);
  // https://github.com/scipy/scipy/blob/6703631bcd15750e86f4098b0421efabcac0f7c2/scipy/stats/stats.py#L3885
  // NOLINTNEXTLINE(readability-magic-numbers, cppcoreguidelines-avoid-magic-numbers)
  return 2.0 * boost::math::cdf<double>(dist, 0.5 * (1 - std::abs(pcc)));
}

double compute_spearman_significance(double rho, std::size_t n) {
  assert(n > 2);  // NOLINT
  const auto dof = static_cast<double>(n - 2);
  const double tscore = rho * std::sqrt(dof / ((1.0 + rho) * (1.0 - rho)));
  boost::math::students_t_distribution<double> dist(dof);
  // https://github.com/scipy/scipy/blob/6703631bcd15750e86f4098b0421efabcac0f7c2/scipy/stats/stats.py#L4229
  // 2 * survival function
  // NOLINTNEXTLINE(readability-magic-numbers, cppcoreguidelines-avoid-magic-numbers)
  return 2.0 * boost::math::cdf<double>(boost::math::complement(dist, std::fabs<double>(tscore)));
}

/*
 * J. Bennett, R. Grout, P. Pebay, D. Roe and D. Thompson, "Numerically stable, single-pass,
 * parallel statistics algorithms," 2009 IEEE International Conference on Cluster Computing and
 * Workshops, New Orleans, LA, 2009, pp. 1-8, doi: 10.1109/CLUSTR.2009.5289161.
 */
template <typename N1, typename N2>
double compute_pearson(absl::Span<const N1> v1, absl::Span<const N2> v2) {
  static_assert(std::is_arithmetic<N1>::value,
                "v1 should be convertible to a Span of numeric type");
  static_assert(std::is_arithmetic<N2>::value,
                "v2 should be convertible to a Span of numeric type");
  DISABLE_WARNING_PUSH
  DISABLE_WARNING_CONVERSION
  double cov = 0;
  double r1_avg = v1[0];
  double r2_avg = v2[0];
  double d1 = 0;
  double d2 = 0;

  for (auto i = 1UL; i < v1.size(); ++i) {
    auto r1_tmp = v1[i] - r1_avg;
    auto r2_tmp = v2[i] - r2_avg;
    d1 += (i * r1_tmp * r1_tmp) / (i + 1);
    d2 += (i * r2_tmp * r2_tmp) / (i + 1);
    cov += i * r1_tmp * r2_tmp / (i + 1);
    r1_avg += r1_tmp / (i + 1);
    r2_avg += r2_tmp / (i + 1);
  }
  DISABLE_WARNING_POP

  // Both datasets are constant (i.e. perfectly correlated)
  if (d1 == 0 && d2 == 0) {
    return 1.0;
  }
  // Only one of the two datasets is constant (i.e. there's no correlation)
  if (d1 == 0 || d2 == 0) {
    return 0.0;
  }

  const auto pcc = std::clamp(cov / std::sqrt(d1 * d2), -1.0, 1.0);

#ifndef DEBUG
  if (std::isnan(pcc)) {
    throw std::logic_error("compute_pearson: pcc cannot be nan!");
  }
#endif

  return pcc;
}

template <typename N1, typename N2>
double compute_pearson(const std::vector<N1>& v1, const std::vector<N2>& v2) {
  return compute_pearson(absl::MakeConstSpan(v1), absl::MakeConstSpan(v2));
}

template <typename N1, typename N2>
double compute_spearman(absl::Span<const N1> v1, absl::Span<const N2> v2) {
  static_assert(std::is_arithmetic<N1>::value,
                "v1 should be convertible to a Span of numeric type");
  static_assert(std::is_arithmetic<N2>::value,
                "v2 should be convertible to a Span of numeric type");
  // Shortcut to avoid computing the correlation when any of the vector is all zeros
  if (std::all_of(v1.begin(), v1.end(), [](auto n) { return n == 0; }) ||
      std::all_of(v2.begin(), v2.end(), [](auto n) { return n == 0; })) {
    return 1.0;
  }
  return compute_pearson(utils::compute_element_ranks(v1), utils::compute_element_ranks(v2));
}

template <typename N1, typename N2>
double compute_spearman(const std::vector<N1>& v1, const std::vector<N2>& v2) {
  return compute_spearman(absl::MakeConstSpan(v1), absl::MakeConstSpan(v2));
}

template <typename N1, typename N2>
std::pair<std::vector<double>, std::vector<double>> compute_corr(absl::Span<const N1> v1,
                                                                 absl::Span<const N2> v2,
                                                                 Algorithm type,
                                                                 std::size_t window_span,
                                                                 std::size_t window_overlap) {
  static_assert(std::is_arithmetic<N1>::value,
                "v1 should be convertible to a Span of numeric type");
  static_assert(std::is_arithmetic<N2>::value,
                "v2 should be convertible to a Span of numeric type");

  if (v1.size() != v2.size()) {
    throw std::runtime_error(
        fmt::format(FMT_STRING("compute_pearson expects a pair of vectors of the same size, got {} "
                               "and {} respectively"),
                    v1.size(), v2.size()));
  }
  assert(window_span > window_overlap);
  std::vector<double> r_vals(v1.size() / (window_span - window_overlap));
  std::vector<double> p_vals(r_vals.size());

  double (*compute_corr_fp)(absl::Span<const N1>, absl::Span<const N2>) = nullptr;
  double (*compute_corr_significance_fp)(double, size_t) = nullptr;
  if (type == Algorithm::pearson) {
    compute_corr_fp = &compute_pearson;
    compute_corr_significance_fp = (&compute_pearson_significance);
  } else if (type == Algorithm::spearman) {
    compute_corr_fp = &compute_spearman;
    compute_corr_significance_fp = (&compute_spearman_significance);
  }
  assert(compute_corr_fp);
  assert(compute_corr_significance_fp);

  for (auto i = 0UL; i < r_vals.size(); ++i) {
    const auto window_start = i * (window_span - window_overlap);
    const auto window_end = window_start + window_span;
    auto slice1 = absl::MakeConstSpan(v1).subspan(window_start, window_end);
    auto slice2 = absl::MakeConstSpan(v2).subspan(window_start, window_end);
    r_vals[i] = compute_corr_fp(slice1, slice2);
    p_vals[i] = compute_corr_significance_fp(r_vals[i], slice1.size());
  }

  return std::make_pair(r_vals, p_vals);
}

template <typename N1, typename N2>
double compute_sed(absl::Span<const N1> v1, absl::Span<const N2> v2) {
  static_assert(std::is_arithmetic<N1>::value,
                "v1 should be convertible to a Span of numeric type");
  static_assert(std::is_arithmetic<N2>::value,
                "v2 should be convertible to a Span of numeric type");

  uint64_t sed{0};
  for (auto i = 0UL; i < v1.size(); ++i) {
    const auto n = v1[i] > v2[i] ? v1[i] - v2[i] : v2[i] - v1[i];
    sed += n * n;
  }
  return static_cast<double>(sed);
}

template <typename N1, typename N2>
double compute_sed(const std::vector<N1>& v1, const std::vector<N2>& v2) {
  return compute_sed(absl::MakeConstSpan(v1), absl::MakeConstSpan(v2));
}

}  // namespace modle::correlation
