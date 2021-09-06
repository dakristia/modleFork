#pragma once

// IWYU pragma: no_include "modle/src/utils/utils_impl.hpp"

#include <xxh3.h>  // for XXH3_freeState, XXH3_state_t

#include <boost/filesystem/file_status.hpp>
#include <boost/filesystem/path.hpp>
#include <cstddef>       // IWYU pragma: keep for size_t
#include <cstdint>       // for int64_t, uint64_t
#include <cstdio>        // for FILE
#include <string>        // for string
#include <string_view>   // for string_view
#include <system_error>  // for errc
#include <type_traits>   // for declval
#include <utility>       // for pair
#include <vector>        // for vector

namespace modle::utils {

template <typename N, typename = std::enable_if_t<std::is_arithmetic_v<N>>>
inline void parse_numeric_or_throw(std::string_view tok, N& field);

template <typename N>
inline void parse_numeric_or_throw(const std::vector<std::string_view>& toks, size_t idx, N& field);

template <typename N>
inline void parse_vect_of_numbers_or_throw(const std::vector<std::string_view>& toks, size_t idx,
                                           std::vector<N>& field, uint64_t expected_size);

template <typename N>
inline void throw_except_from_errc(std::string_view tok, size_t idx, const N& field, const char* c,
                                   std::errc e);

[[nodiscard]] inline bool chrom_equal_operator(std::string_view chr1, std::string_view chr2);
[[nodiscard]] inline bool chrom_equal_operator(const std::pair<std::string_view, int64_t>& chr1,
                                               const std::pair<std::string_view, int64_t>& chr2);

[[nodiscard]] inline bool chrom_less_than_operator(std::string_view chr1, std::string_view chr2);
[[nodiscard]] inline bool chrom_less_than_operator(
    const std::pair<std::string_view, int64_t>& chr1,
    const std::pair<std::string_view, int64_t>& chr2);

// Typetraits stuff

// Usage: static_assert(std::experimental::is_detected_convertible_v<const char*,
// utils::has_data_member_func, T>, "error msg");
// This static_assert will fail for types that don't have a member function .data() that returns a
// const char*

template <class T>
using has_data_member_func = decltype(std::declval<T&>().data());

template <class T>
using has_size_member_func = decltype(std::declval<T&>().size());

template <typename T>
[[nodiscard]] inline std::string get_printable_type_name(const T& var);

// https://stackoverflow.com/a/56766138
template <typename T>
constexpr auto get_printable_type_name() noexcept;

// Various

struct XXH3_Deleter {  // NOLINT
  inline void operator()(XXH3_state_t* state) noexcept;
};

inline void fclose(FILE* fp) noexcept(false);

// Try to convert str representations like "1.0" or "1.000000" to "1"
[[nodiscard]] inline std::string str_float_to_str_int(const std::string& s);

// Returns false in case a collision was detected
inline bool detect_path_collision(
    const boost::filesystem::path& p, std::string& error_msg, bool force_overwrite = false,
    boost::filesystem::file_type expected_type = boost::filesystem::regular_file);
[[nodiscard]] inline std::string detect_path_collision(
    const boost::filesystem::path& p, bool force_overwrite = false,
    boost::filesystem::file_type expected_type = boost::filesystem::regular_file);

struct identity {
  template <class T>
  [[nodiscard]] constexpr T&& operator()(T&& a) const noexcept;
  using is_transparent = void;
};

[[maybe_unused]] [[nodiscard]] constexpr bool ndebug_defined() noexcept;
[[maybe_unused]] [[nodiscard]] constexpr bool ndebug_not_defined() noexcept;

#ifdef __has_cpp_attribute
#if __has_cpp_attribute(clang::no_destroy)
#define MODLE_NO_DESTROY [[clang::no_destroy]]
#else
#define MODLE_NO_DESTROY
#endif
#else
#define MODLE_NO_DESTROY
#endif

// Source: https://www.youtube.com/watch?v=INn3xa4pMfg
template <class Key, class Value, size_t Size>
class ConstMap {
  std::array<std::pair<Key, Value>, Size> _buff;
  using const_iterator = typename std::array<std::pair<Key, Value>, Size>::const_iterator;

 public:
  template <class... Args>
  constexpr ConstMap(Args&&... args) noexcept;
  [[nodiscard]] constexpr const Value& at(const Key& key) const;
  [[nodiscard]] constexpr const Value& operator[](const Key& key) const;
  [[nodiscard]] constexpr const_iterator find(const Key& key) const noexcept;
  [[nodiscard]] constexpr bool contains(const Key& key) const noexcept;

  [[nodiscard]] constexpr const_iterator begin() const noexcept;
  [[nodiscard]] constexpr const_iterator end() const noexcept;
};

}  // namespace modle::utils

#include "../../../utils_impl.hpp"  // IWYU pragma: export
