#pragma once

#include <absl/strings/match.h>        // for EndsWith
#include <absl/strings/str_cat.h>      // for StrAppend, StrCat
#include <absl/strings/string_view.h>  // for string_view
#include <absl/strings/strip.h>        // for StripSuffix
#include <fmt/format.h>                // for format, FMT_STRING, print
#include <fmt/ostream.h>               // for formatbuf<>::int_type

#include <CLI/App.hpp>                // for Option_group, App
#include <CLI/Config.hpp>             // IWYU pragma: keep for ConfigBase
#include <CLI/Error.hpp>              // for ParseError
#include <CLI/Formatter.hpp>          // IWYU pragma: keep for Formatter
#include <CLI/Option.hpp>             // for Option
#include <CLI/Validators.hpp>         // for PositiveNumber, NonNegativeNumber, Range, Existing...
#include <boost/filesystem/path.hpp>  // for path, exists, operator<<, is_empty, is_directory
#include <cmath>                      // for round
#include <cstdint>                    // for uint32_t, uint64_t
#include <limits>                     // for numeric_limits
#include <sstream>                    // for basic_stringbuf<>::int_type, basic_stringbuf<>::po...
#include <stdexcept>                  // for invalid_argument, out_of_range
#include <string>                     // for allocator, string, basic_string

#include "cli.hpp"                  // for FMT_COMPILE_STRING
#include "modle/common/common.hpp"  // for bp_t
#include "modle/common/config.hpp"  // for Config

namespace modle {

class Cli {
  enum subcommand : uint8_t { simulate, pertubate };

 private:
  int _argc;
  char** _argv;
  std::string _exec_name;
  Config _config;
  CLI::App _cli{};

  void make_cli();
  void make_simulation_subcommand();
  void make_perturbate_subcommand();

 public:
  Cli(int argc, char** argv);
  [[nodiscard]] const Config& parse_arguments();
  [[nodiscard]] static std::string process_paths_and_check_for_collisions(modle::Config& c);

  [[nodiscard]] int exit(const CLI::ParseError& e) const;

 private:
  void validate_and_transform_args();
};

}  // namespace modle
