#include "modle_tools/cli.hpp"
#include "modle_tools/tools.hpp"

int main(int argc, char** argv) {
  modle::tools::Cli cli(argc, argv);
  auto c = cli.parse_arguments();
  if (!cli.is_ok()) {
    return c.exit_code;
  }

  std::filesystem::create_directories(c.tmp_dir);
  std::filesystem::create_directories(c.out_dir);
  try {
    switch (cli.get_subcommand()) {
      case modle::tools::Cli::subcommand::convert:
        modle::tools::convert(c);
        break;
      case modle::tools::Cli::subcommand::eval:
        modle::tools::eval(c);
        break;
      default:
        throw std::runtime_error(
            "Default branch in switch statement in main.cpp should be unreachable! If you see "
            "this message, please open an issue on GitHub");
    }
  } catch (const std::runtime_error& err) {
    fmt::print(stderr, "FAILURE: {}.\n", err.what());
    std::error_code ec;
    if (!c.keep_tmp_files && std::filesystem::is_empty(c.tmp_dir)) {
      std::filesystem::remove_all(c.tmp_dir, ec);
    }
    return 1;
  }

  try {
    if (!c.keep_tmp_files &&
        (!std::filesystem::exists(c.tmp_dir) || std::filesystem::is_empty(c.tmp_dir))) {
      std::filesystem::remove_all(c.tmp_dir);
    }
  } catch (const std::filesystem::filesystem_error& err) {
    fmt::print(stderr, "An error occurred while removing the temporary directory '{}': {}.\n",
               c.tmp_dir, err.what());
    return 1;
  }

  return 0;
}
