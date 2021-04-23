#pragma once

// IWYU pragma: private, include "modle/contacts.hpp"

#include <absl/strings/str_join.h>        // for StrJoin
#include <absl/types/span.h>              // for MakeConstSpan, Span
#include <cpp-sort/sorters/ska_sorter.h>  // for ska_sort
#include <cpp-sort/utility/functional.h>  // for as_projection
#include <fmt/format.h>                   // for FMT_STRING, format

#include <algorithm>                                // for max, fill, copy
#include <atomic>                                   // for memory_order_relaxed
#include <boost/dynamic_bitset/dynamic_bitset.hpp>  // for dynamic_bitset
#include <boost/exception/exception.hpp>            // IWYU pragma: keep for error_info_base
#include <cassert>                                  // for assert
#include <cmath>                                    // for round
#include <cstdint>                                  // for uint64_t, int64_t
#include <fstream>                                  // for size_t
#include <limits>                                   // for numeric_limits
#include <mutex>                                    // for mutex
#include <random>                                   // for normal_distribution, random_device
#include <stdexcept>                                // for runtime_error, logic_error
#include <type_traits>                              // for is_integral, is_signed
#include <utility>                                  // for make_pair, pair
#include <vector>                                   // for vector, allocator

#include "modle/common.hpp"                      // for modle::PRNG, modle::seeder
#include "modle/suppress_compiler_warnings.hpp"  // for DISABLE_WARNING_POP, DISABLE_WARNI...
#include "modle/utils.hpp"                       // for ndebug_defined, throw_with_trace

namespace modle {
template <typename I>
ContactMatrix<I>::ContactMatrix(const ContactMatrix<I> &other)
    : _nrows(other.nrows()),
      _ncols(other.ncols()),
      _contacts(other._contacts),
      _tot_contacts(other.get_tot_contacts()),
      _updates_missed(other.get_n_of_missed_updates()),
      _locks(other._locks.size()) {}

template <typename I>
ContactMatrix<I>::ContactMatrix(size_t nrows, size_t ncols, bool fill_with_random_numbers)
    : _nrows(nrows), _ncols(ncols), _contacts(_nrows * _ncols + 1, 0), _locks(_ncols) {
  if (fill_with_random_numbers) {
    std::random_device rand_dev;
    auto rand_eng = PRNG(rand_dev());
    std::uniform_int_distribution<I> dist{0, std::numeric_limits<I>::max()};
    for (auto i = 0UL; i < _ncols; ++i) {
      for (auto j = i; j < i + _nrows && j < _ncols; ++j) {
        this->set(i, j, dist(rand_eng));
      }
    }
  }
}

template <typename I>
ContactMatrix<I> &ContactMatrix<I>::operator=(const ContactMatrix<I> &other) {
  this->_nrows = other.nrows();
  this->_ncols = other.ncols();
  if (!this->_contacts.empty()) {
    this->_contacts.resize(other._contacts.size());
    std::copy(other._contacts.begin(), other._contacts.end(), this->_contacts.begin());
  } else {
    this->_contacts = other._contacts;
  }
  this->_tot_contacts = other._tot_contacts.load();
  this->_updates_missed = other._updates_missed.load();
  this->_locks.resize(other._locks.size());

  return *this;
}

template <typename I>
I ContactMatrix<I>::get(size_t row, size_t col) const noexcept(utils::ndebug_defined()) {
  const auto [i, j] = transpose_coords(row, col);
#ifndef NDEBUG
  if (i >= this->ncols() || j >= this->ncols()) {
    utils::throw_with_trace(std::logic_error(fmt::format(
        FMT_STRING("ContactMatrix<I>::get(row={}, col={}) tried to access an element outside of "
                   "the space {}x{}, which is what this contact matrix is supposed to represent"),
        row, col, col, col)));
  }
#endif

  if (i >= this->nrows()) {
    return 0;
  }

  return this->at(i, j);
}

template <typename I>
template <typename I2>
void ContactMatrix<I>::set(size_t row, size_t col, I2 n) noexcept(utils::ndebug_defined()) {
  static_assert(std::is_integral<I2>::value,
                "ContactMatrix<I>::set expects the parameter n to be an integer type.");
#ifndef NDEBUG
  DISABLE_WARNING_PUSH
  DISABLE_WARNING_SIGN_COMPARE
  DISABLE_WARNING_BOOL_COMPARE
  if (n < std::numeric_limits<I>::min() || n > std::numeric_limits<I>::max()) {
    utils::throw_with_trace(std::runtime_error(
        fmt::format(FMT_STRING("ContactMatrix<I>::set(row={}, col={}, n={}): Overflow detected: "
                               "n={} is outside the range of representable numbers ({}-{})"),
                    row, col, n, n, std::numeric_limits<I>::min(), std::numeric_limits<I>::max())));
  }
  DISABLE_WARNING_POP
#endif
  const auto [i, j] = transpose_coords(row, col);

#ifndef NDEBUG
  try {
    this->bound_check_column(j);
#endif
    if (i > this->nrows()) {
      this->_updates_missed.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    std::scoped_lock l(this->_locks[j]);
    auto &m = this->at(i, j);
    DISABLE_WARNING_PUSH
    DISABLE_WARNING_CONVERSION
    DISABLE_WARNING_SIGN_CONVERSION
    DISABLE_WARNING_SIGN_COMPARE
#ifndef NDEBUG
    assert(this->_tot_contacts >= m);
    if constexpr (std::is_signed_v<I2>) {
      if (n < 0) {
        utils::throw_with_trace(std::runtime_error(fmt::format(
            FMT_STRING("Setting counts to a negative value (n={}) is not allowed"), n)));
      }
      assert(this->_tot_contacts - m <
             std::numeric_limits<decltype(this->_tot_contacts)>::max() - n);
    }
#endif
    if (n > m) {
      this->_tot_contacts += n - m;
    } else {
      this->_tot_contacts -= m - n;
    }
    m = n;
    DISABLE_WARNING_POP
#ifndef NDEBUG
  } catch (const std::runtime_error &err) {
    utils::throw_with_trace(std::logic_error(
        fmt::format(FMT_STRING("ContactMatrix::set({}, {}, {}): {})"), row, col, n, err.what())));
  }
#endif
}

template <typename I>
template <typename I2>
void ContactMatrix<I>::add(size_t row, size_t col, I2 n) noexcept(utils::ndebug_defined()) {
  static_assert(std::is_integral<I2>::value,
                "ContactMatrix<I>::add expects the parameter n to be an integer type.");

  const auto [i, j] = transpose_coords(row, col);

#ifndef NDEBUG
  try {
    this->bound_check_column(j);
#endif
    if (i > this->nrows()) {
      this->_updates_missed.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    std::scoped_lock l(this->_locks[j]);
    DISABLE_WARNING_PUSH
    DISABLE_WARNING_SIGN_COMPARE
    DISABLE_WARNING_SIGN_CONVERSION
#ifndef NDEBUG
    this->check_for_overflow_on_add(i, j, n);
#endif

    this->at(i, j) += n;
    this->_tot_contacts += n;
    DISABLE_WARNING_POP
#ifndef NDEBUG
  } catch (const std::runtime_error &err) {
    utils::throw_with_trace(std::logic_error(fmt::format(
        FMT_STRING("ContactMatrix<I>::add(row={}, col={}, n={}): {}"), row, col, n, err.what())));
  }
#endif
}

template <typename I>
template <typename I2>
void ContactMatrix<I>::subtract(size_t row, size_t col, I2 n) noexcept(utils::ndebug_defined()) {
  static_assert(std::is_integral<I2>::value,
                "ContactMatrix<I>::subtract expects the parameter n to be an integer type.");

  const auto [i, j] = transpose_coords(row, col);

#ifndef NDEBUG
  try {
    this->bound_check_column(j);
#endif
    if (i > this->nrows()) {
      this->_updates_missed.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    std::scoped_lock l(this->_locks[j]);
#ifndef NDEBUG
    this->check_overflow_on_subtract(i, j, n);
#endif
    assert(n >= 0);  // NOLINT
    this->at(i, j) -= static_cast<I>(n);
    this->_tot_contacts -= static_cast<size_t>(n);
#ifndef NDEBUG
  } catch (const std::runtime_error &err) {
    utils::throw_with_trace(std::logic_error(fmt::format(
        FMT_STRING("ContactMatrix::subtract({}, {}, {}): {})"), row, col, n, err.what())));
  }
#endif
}

// IMPORTANT! This function modifies the pixel buffer
template <typename I>
template <typename I2>
void ContactMatrix<I>::add(absl::Span<std::pair<size_t /* rows */, size_t /* cols */>> pixels, I2 n,
                           size_t size_thresh) noexcept(utils::ndebug_defined()) {
  std::transform(pixels.begin(), pixels.end(), pixels.begin(),
                 [](const auto &pixel) { return transpose_coords(pixel.first, pixel.second); });
  cppsort::ska_sort(pixels.begin(), pixels.end(),
                    &std::remove_reference_t<decltype(pixels.front())>::second);

  if (pixels.size() < size_thresh) {
    this->add_small_buff(pixels, n);
  } else {
    this->add_large_buff(pixels, n);
  }
}

template <typename I>
template <typename I2>
void ContactMatrix<I>::add_small_buff(
    absl::Span<std::pair<size_t /* rows */, size_t /* cols */>> pixels,
    I2 n) noexcept(utils::ndebug_defined()) {
  static_assert(std::is_integral<I2>::value,
                "ContactMatrix<I>::add expects the parameter n to be an integer type.");

  for (const auto &[row, col] : pixels) {
#ifndef NDEBUG
    try {
      bound_check_column(col);
#endif
      if (row > this->nrows()) {
        this->_updates_missed.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      std::scoped_lock l(this->_locks[col]);
#ifndef NDEBUG
      this->check_for_overflow_on_add(row, col, n);
#endif
      this->at(row, col) += n;
      this->_tot_contacts += n;
#ifndef NDEBUG
    } catch (const std::runtime_error &err) {
      utils::throw_with_trace(std::logic_error(fmt::format(
          FMT_STRING("ContactMatrix<I>::add(row={}, col={}, n={}): {}"), row, col, n, err.what())));
    }
#endif
  }
}

template <typename I>
template <typename I2>
void ContactMatrix<I>::add_large_buff(
    absl::Span<std::pair<size_t /* rows */, size_t /* cols */>> pixels,
    I2 n) noexcept(utils::ndebug_defined()) {
  static_assert(std::is_integral<I2>::value,
                "ContactMatrix<I>::add expects the parameter n to be an integer type.");

  auto ncontacts = 0UL;
  auto missed_updates = 0UL;
  auto range = std::make_pair(pixels.begin(), pixels.begin());
  while (range.second != pixels.end()) {
#ifndef NDEBUG
    try {
#endif
      range = std::equal_range(
          range.first, pixels.end(), *range.second,
          [](const auto &pixel1, const auto &pixel2) { return pixel1.second < pixel2.second; });
      this->bound_check_column(range.first->second);
      std::scoped_lock l(this->_locks[range.first->second]);
      while (range.first != range.second) {
        const auto &row = range.first->first;
        const auto &col = range.first->second;

        if (row > this->nrows()) {
          missed_updates += 1;
          ++range.first;
          continue;
        }
#ifndef NDEBUG
        this->check_for_overflow_on_add(row, col, n);
#endif
        this->at(row, col) += n;
        ++ncontacts;
        ++range.first;
      }
#ifndef NDEBUG
    } catch (const std::runtime_error &err) {
      this->_updates_missed.fetch_add(missed_updates, std::memory_order_relaxed);
      this->_tot_contacts += ncontacts;
      utils::throw_with_trace(std::logic_error(
          fmt::format(FMT_STRING("ContactMatrix<I>::add(row={}, col={}, n={}): {}"),
                      range.first->first, range.first->second, n, err.what())));
    }
#endif
  }
  this->_updates_missed.fetch_add(missed_updates, std::memory_order_relaxed);
  this->_tot_contacts += ncontacts;
}

template <typename I>
void ContactMatrix<I>::increment(size_t row, size_t col) noexcept(utils::ndebug_defined()) {
  this->add(row, col, static_cast<I>(1));
}

template <typename I>
void ContactMatrix<I>::increment(absl::Span<std::pair<size_t /* rows */, size_t /* cols */>> pixels,
                                 size_t size_thresh) noexcept(utils::ndebug_defined()) {
  this->add(pixels, static_cast<I>(1), size_thresh);
}

template <typename I>
void ContactMatrix<I>::decrement(size_t row, size_t col) noexcept(utils::ndebug_defined()) {
  this->subtract(row, col, static_cast<I>(1));
}

template <typename I>
constexpr size_t ContactMatrix<I>::ncols() const noexcept(utils::ndebug_defined()) {
  return this->_ncols;
}

template <typename I>
constexpr size_t ContactMatrix<I>::nrows() const noexcept(utils::ndebug_defined()) {
  return this->_nrows;
}

template <typename I>
constexpr size_t ContactMatrix<I>::npixels() const noexcept(utils::ndebug_defined()) {
  return this->_nrows * this->_ncols;
}

template <typename I>
size_t ContactMatrix<I>::npixels_after_masking() const {
  auto npixels = this->npixels();
  const auto mask = this->generate_mask_for_bins_without_contacts();
  if (mask.all()) {
    return npixels;
  }
  if (mask.none()) {
    return 0;
  }

  auto count_zeros = [&mask](size_t start, size_t end) {
    assert(start <= end);
    size_t n = 0;
    while (start < end) {
      n += !mask[start++];
    }
    return n;
  };

  assert(this->nrows() <= this->ncols());
  for (auto i = 0UL; i < this->ncols(); ++i) {
    if (!mask[i]) {
      // We are processing pixels in the upper left corner of cmatrix
      if (i < this->nrows()) {
        npixels -= this->nrows() - count_zeros(0UL, i);
        npixels -= i;
        assert(npixels <= this->npixels());
        // We are processing pixels in the lower right corner of cmatrix
      } else if (i > this->ncols() - this->nrows()) {
        npixels -= this->nrows() - count_zeros(i - this->nrows(), i);
        npixels -= this->ncols() - i;
        assert(npixels <= this->npixels());
      } else {  // We are processing pixels in the center of the cmatrix
        npixels -= (2 * this->nrows()) - 1 - count_zeros(i - this->nrows(), i);
        assert(npixels <= this->npixels());
      }
    }
  }
  return npixels;
}

template <typename I>
constexpr size_t ContactMatrix<I>::get_n_of_missed_updates() const
    noexcept(utils::ndebug_defined()) {
  return this->_updates_missed;
}

template <typename I>
constexpr size_t ContactMatrix<I>::get_tot_contacts() const noexcept(utils::ndebug_defined()) {
  return this->_tot_contacts;
}

template <typename I>
double ContactMatrix<I>::get_avg_contact_density() const noexcept(utils::ndebug_defined()) {
  return static_cast<double>(this->get_tot_contacts()) /
         static_cast<double>(this->_contacts.size());
}

template <typename I>
constexpr size_t ContactMatrix<I>::get_matrix_size_in_bytes() const
    noexcept(utils::ndebug_defined()) {
  return this->_contacts.size() * sizeof(I);
}

template <typename I>
constexpr double ContactMatrix<I>::get_matrix_size_in_mb() const noexcept(utils::ndebug_defined()) {
  return static_cast<double>(this->get_matrix_size_in_bytes()) / 1.0e6;
}

template <typename I>
void ContactMatrix<I>::print(bool full) const {
  if (full) {
    std::vector<I> row(this->_ncols, 0);
    for (size_t y = 0; y < this->_ncols; ++y) {
      std::fill(row.begin(), row.end(), 0);
      for (size_t x = 0; x < this->_ncols; ++x) {
        auto j = x;
        auto i = j - y;
        if (y > x) {
          j = y;
          i = j - x;
        }
        if (i >= this->_nrows) {
          row[x] = 0;
        } else {
          row[x] = this->at(i, j);
        }
      }
      fmt::print(FMT_STRING("{}\n"), absl::StrJoin(row, "\t"));
    }
  } else {
    std::vector<I> row(this->ncols());
    for (auto i = 0UL; i < this->nrows(); ++i) {
      for (auto j = 0UL; j < this->ncols(); ++j) {
        row[j] = this->at(i, j);
      }
      fmt::print(FMT_STRING("{}\n"), absl::StrJoin(row, "\t"));
    }
  }
}

template <typename I>
std::vector<std::vector<I>> ContactMatrix<I>::generate_symmetric_matrix() const {
  std::vector<std::vector<I>> m;
  m.reserve(this->_ncols);
  for (size_t y = 0; y < this->_ncols; ++y) {
    std::vector<I> row(this->_ncols, 0);
    for (size_t x = 0; x < this->_ncols; ++x) {
      auto j = x;
      auto i = j - y;
      if (y > x) {
        j = y;
        i = j - x;
      }

      if (i < this->_nrows) {
        row[x] = this->at(i, j);
      }
    }
    m.emplace_back(std::move(row));
  }
  return m;
}

template <typename I>
void ContactMatrix<I>::generate_mask_for_bins_without_contacts(
    boost::dynamic_bitset<> &mask) const {
  mask.resize(this->ncols());
  mask.reset();
  for (auto i = 0UL; i < this->ncols(); ++i) {
    // Set bitmask to 1 if row contains at least one non-zero value
    for (auto j = i; j < (i + this->nrows()) && j < this->ncols(); ++j) {
      if ((mask[i] |= this->get(i, j))) {
        break;
      }
    }
    // Set bitmask to 1 if column "above" the current bin contains at least one non-zero value
    for (auto j = i; j > 0 && j > (i - this->nrows()); --j) {
      if ((mask[i] |= this->get(i, j))) {
        break;
      }
    }
  }
}

template <typename I>
boost::dynamic_bitset<> ContactMatrix<I>::generate_mask_for_bins_without_contacts() const {
  boost::dynamic_bitset<> mask{};
  this->generate_mask_for_bins_without_contacts(mask);
  return mask;
}

template <typename I>
void ContactMatrix<I>::clear_missed_updates_counter() {
  this->_updates_missed = 0;
}

template <typename I>
void ContactMatrix<I>::reset() {
  std::fill(this->_contacts.begin(), this->_contacts.end(), 0);
  this->_tot_contacts = 0;
  this->_updates_missed = 0;
}

template <typename I>
void ContactMatrix<I>::resize(size_t nrows, size_t ncols) {
  this->_nrows = nrows;
  this->_ncols = ncols;
  this->_contacts.resize((this->nrows() * this->ncols()) + 1, 0);
  std::vector<std::mutex> locks(this->ncols());
  std::swap(this->_locks, locks);
}

template <typename I>
bool ContactMatrix<I>::empty() const {
  assert(std::all_of(this->_contacts.begin(), this->_contacts.end(),
                     [](const auto n) { return n == 0; }));
  return this->_tot_contacts == 0;
}

template <typename I>
absl::Span<const I> ContactMatrix<I>::get_raw_count_vector() const {
  return absl::MakeConstSpan(this->_contacts);
}

template <typename I>
void ContactMatrix<I>::compute_row_wise_contact_histogram(std::vector<uint64_t> &buff) const {
  buff.resize(this->nrows());
  std::fill(buff.begin(), buff.end(), 0);

  for (auto i = 0UL; i < this->ncols(); ++i) {
    for (auto j = i; j < i + this->nrows() && j < this->ncols(); ++j) {
      // j - i corresponds to the distance from the diagonal
      buff[j - i] += this->get(j, i);
    }
  }
}

template <typename I>
std::vector<uint64_t> ContactMatrix<I>::compute_row_wise_contact_histogram() const {
  std::vector<uint64_t> buff(this->nrows());
  this->compute_row_wise_contact_histogram(buff);
  return buff;
}

template <typename I>
void ContactMatrix<I>::deplete_contacts(double depletion_multiplier) {
  const auto hist = this->compute_row_wise_contact_histogram();
  const auto effective_nbins = this->generate_mask_for_bins_without_contacts().count();
  // This histogram contains the average contact number (instead of the total)
  std::vector<uint64_t> row_wise_avg_contacts(hist.size());
  std::transform(hist.begin(), hist.end(), row_wise_avg_contacts.begin(), [&](const auto n) {
    return static_cast<uint64_t>(std::round((depletion_multiplier * static_cast<double>(n)) /
                                            static_cast<double>(effective_nbins)));
  });

  for (auto i = 0UL; i < this->ncols(); ++i) {
    for (auto j = i; j < i + this->nrows() && j < this->ncols(); ++j) {
      // j - i corresponds to the distance from the diagonal
      if (this->get(j, i) > row_wise_avg_contacts[j - i]) {
        this->subtract(j, i, row_wise_avg_contacts[j - i]);
      } else {
        this->set(j, i, 0);
      }
    }
  }
}

template <typename I>
I &ContactMatrix<I>::at(size_t i, size_t j) noexcept(utils::ndebug_defined()) {
#ifndef NDEBUG
  if ((j * this->_nrows) + i > this->_contacts.size()) {
    utils::throw_with_trace(std::runtime_error(fmt::format(
        "ContactMatrix::at tried to access element m[{}][{}] of a matrix of shape [{}][{}]! "
        "({} >= {})",
        i, j, this->nrows(), this->ncols(), (j * this->_nrows) + i, this->_contacts.size())));
  }
#endif
  return this->_contacts[(j * this->_nrows) + i];
}

template <typename I>
const I &ContactMatrix<I>::at(size_t i, size_t j) const noexcept(utils::ndebug_defined()) {
#ifndef NDEBUG
  if ((j * this->_nrows) + i > this->_contacts.size()) {
    utils::throw_with_trace(std::runtime_error(fmt::format(
        "ContactMatrix::at tried to access element m[{}][{}] of a matrix of shape [{}][{}]! "
        "({} >= {})",
        i, j, this->nrows(), this->ncols(), (j * this->_nrows) + i, this->_contacts.size())));
  }
#endif
  return this->_contacts[(j * this->_nrows) + i];
}

template <typename I>
std::pair<size_t, size_t> ContactMatrix<I>::transpose_coords(size_t row, size_t col) noexcept(
    utils::ndebug_defined()) {
  if (row > col) {
    return std::make_pair(row - col, row);
  }
  return std::make_pair(col - row, col);
}

template <typename I>
void ContactMatrix<I>::bound_check_column(size_t col) const {
  if (col > this->ncols()) {
    utils::throw_with_trace(std::runtime_error(fmt::format(
        FMT_STRING("caught an attempt to access element past the end of the contact matrix: "
                   "j={}; ncols={}: j > ncols"),
        col, this->ncols())));
  }
}

template <typename I>
template <typename I2>
void ContactMatrix<I>::check_for_overflow_on_add(size_t row, size_t col, I2 n) const {
  static_assert(std::is_integral_v<I2>, "n should be an integral number.");

  DISABLE_WARNING_PUSH
  DISABLE_WARNING_SIGN_COMPARE
  DISABLE_WARNING_SIGN_CONVERSION
  auto &m = this->at(row, col);
  if constexpr (std::is_signed<I2>::value) {
    if (n < 0) {
      utils::throw_with_trace(
          std::logic_error("Consider using ContactMatrix<I>::subtract instead of "
                           "incrementing by a negative number."));
    }
  }
  if (std::numeric_limits<I>::max() - n < m) {
    utils::throw_with_trace(std::runtime_error(
        fmt::format(FMT_STRING("Overflow detected: incrementing m={} by n={} would result in a "
                               "number outside of range {}-{}"),
                    m, n, std::numeric_limits<I>::min(), std::numeric_limits<I>::max())));
  }
  if (std::numeric_limits<I>::max() - n < this->_tot_contacts) {
    utils::throw_with_trace(std::runtime_error(fmt::format(
        FMT_STRING("Overflow detected: incrementing _tot_contacts={} by n={} would result in a "
                   "number outside of range {}-{}"),
        this->_tot_contacts, n, std::numeric_limits<I>::min(), std::numeric_limits<I>::max())));
  }
  DISABLE_WARNING_POP
}

template <typename I>
template <typename I2>
void ContactMatrix<I>::check_overflow_on_subtract(size_t row, size_t col, I2 n) const {
  DISABLE_WARNING_PUSH
  DISABLE_WARNING_CONVERSION
  DISABLE_WARNING_SIGN_COMPARE
  DISABLE_WARNING_SIGN_CONVERSION
  auto &m = this->at(row, col);
  if constexpr (std::is_signed<I2>::value) {
    if (n < 0) {
      utils::throw_with_trace(std::logic_error(fmt::format(
          FMT_STRING("ContactMatrix<I>::subtract(row={}, col={}, n={}): consider using "
                     "ContactMatrix<I>::add instead of decrementing by a negative number."),
          row, col, n)));
    }
  }

  if (std::numeric_limits<I>::min() + n > m) {
    utils::throw_with_trace(std::runtime_error(
        fmt::format(FMT_STRING("Overflow detected: decrementing m={} by n={} would result in a "
                               "number outside of range {}-{}"),
                    m, n, std::numeric_limits<I>::min(), std::numeric_limits<I>::max())));
  }
  if (std::numeric_limits<I>::min() + n > this->_tot_contacts) {
    utils::throw_with_trace(std::runtime_error(fmt::format(
        FMT_STRING("Overflow detected: decrementing _tot_contacts={} by n={} would result in a "
                   "number outside of range {}-{}"),
        this->_tot_contacts, n, std::numeric_limits<I>::min(), std::numeric_limits<I>::max())));
  }
}

}  // namespace modle
