// Copyright (C) 2022 Roberto Rossini <roberros@uio.no>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <absl/container/fixed_array.h>

#include <boost/filesystem/file_status.hpp>  // for regular_file, file_type
#include <boost/filesystem/path.hpp>         // for path
#include <initializer_list>
#include <range/v3/view/map.hpp>
#include <string>       // for string
#include <string_view>  // for string_view

namespace modle::utils {

// Try to convert str representations like "1.0" or "1.000000" to "1"
[[nodiscard]] inline std::string str_float_to_str_int(const std::string& s);

[[nodiscard]] inline std::string replace_non_alpha_char(const std::string& s, char replacement);
template <char replacement = '_'>
[[nodiscard]] inline std::string replace_non_alpha_char(const std::string& s);

template <class Collection>
[[nodiscard]] inline std::string format_collection_to_english_list(const Collection& collection,
                                                                   std::string_view sep = ", ",
                                                                   std::string_view last_sep = "");

// Returns false in case a collision was detected
inline bool detect_path_collision(
    const boost::filesystem::path& p, std::string& error_msg, bool force_overwrite = false,
    boost::filesystem::file_type expected_type = boost::filesystem::regular_file);
[[nodiscard]] inline std::string detect_path_collision(
    const boost::filesystem::path& p, bool force_overwrite = false,
    boost::filesystem::file_type expected_type = boost::filesystem::regular_file);

// This class stores pairs of string labels and enums and provides the in a way that is compatible
// with CLI11
template <class EnumT, class StringT = std::string>
class CliEnumMappings {
  static_assert(std::is_convertible_v<StringT, std::string>);

 private:
  absl::FixedArray<std::pair<StringT, EnumT>> _mappings;

 public:
  using key_type = StringT;
  using mapped_type = EnumT;
  using value_type = std::pair<key_type, mapped_type>;
  using reference = typename decltype(_mappings)::const_reference;
  using const_reference = typename decltype(_mappings)::const_reference;
  using pointer = typename decltype(_mappings)::const_pointer;
  using const_pointer = typename decltype(_mappings)::const_pointer;
  using size_type = typename decltype(_mappings)::size_type;
  using iterator = typename decltype(_mappings)::const_iterator;
  using const_iterator = typename decltype(_mappings)::const_iterator;

  inline CliEnumMappings() = default;
  inline CliEnumMappings(std::initializer_list<value_type> mappings, bool sort_by_key = true);
  inline CliEnumMappings(std::initializer_list<StringT> labels, std::initializer_list<EnumT> enums,
                         bool sort_by_key = true);

  [[nodiscard]] inline const_iterator begin() const;
  [[nodiscard]] inline const_iterator end() const;
  [[nodiscard]] inline const_iterator cbegin() const;
  [[nodiscard]] inline const_iterator cend() const;

  [[nodiscard]] inline const_iterator find(EnumT key) const;
  [[nodiscard]] inline const_iterator find(const StringT& key) const;

  [[nodiscard]] inline const StringT& at(EnumT key) const;
  [[nodiscard]] inline EnumT at(const StringT& key) const;

  [[nodiscard]] inline auto keys_view() const -> decltype(ranges::views::keys(this->_mappings));
  [[nodiscard]] inline auto values_view() const -> decltype(ranges::views::values(this->_mappings));
};

}  // namespace modle::utils

#include "../../../cli_utils_impl.hpp"  // IWYU pragma: export
