#include <filesystem>
#include <string>

#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "modle/cli.hpp"
#include "modle/genome.hpp"
#include "modle/parsers.hpp"

namespace modle {

std::vector<std::string> check_if_output_file_exists(const modle::config& c) {
  std::vector<std::string> fn_collisions;
  if (c.force) return fn_collisions;
  modle::ChrSizeParser parser(c.path_to_chr_sizes);
  for (const auto& record : parser.parse()) {
    const auto f1 = absl::StrFormat("%s/%s.tsv.bz2", c.output_dir, record.name);
    const auto f2 = absl::StrFormat("%s/%s_raw.tsv.bz2", c.output_dir, record.name);
    if (std::filesystem::exists(f1)) {
      fn_collisions.push_back(std::filesystem::weakly_canonical(f1));
    }
    if (std::filesystem::exists(f2)) {
      fn_collisions.push_back(std::filesystem::weakly_canonical(f2));
    }
  }

  return fn_collisions;
}

void run_simulation(const modle::config& c) {
  auto t0 = absl::Now();
  modle::Genome genome(c);

  uint64_t tot_barriers = c.number_of_randomly_gen_extr_barriers;
  uint64_t barriers_ignored = 0;
  if (c.number_of_randomly_gen_extr_barriers != 0) {
    genome.randomly_generate_extrusion_barriers(c.number_of_randomly_gen_extr_barriers);
  }
  if (!c.path_to_extr_barriers_bed.empty()) {
    const auto tmp = genome.import_extrusion_barriers_from_bed(
        c.path_to_extr_barriers_bed, c.probability_of_extrusion_barrier_block);
    tot_barriers += tmp.first;
    barriers_ignored = tmp.second;
  }
  absl::FPrintF(stderr,
                "Initialization took %s.\n"
                " - # of sequences:       %lu\n"
                " - Avg. sequence length: %.3f Mbp\n"
                " - Genome N50:           %.3f Mbp\n"
                " - # of LEFs:            %lu\n"
                " - # of extr. barriers   %lu (%lu ignored)\n",
                absl::FormatDuration(absl::Now() - t0), genome.get_n_chromosomes(),
                (static_cast<double>(genome.size()) / genome.get_n_chromosomes()) / 1.0e6,
                genome.n50() / 1.0e6, genome.get_n_lefs(), tot_barriers - barriers_ignored,
                barriers_ignored);

  t0 = absl::Now();
  if (c.skip_burnin) {
    genome.randomly_bind_lefs();
    absl::FPrintF(stderr, "Bound %lu LEFs in %s.\n", genome.get_n_of_busy_lefs(),
                  absl::FormatDuration(absl::Now() - t0));
  } else {
    const auto burnin_rounds = genome.run_burnin(
        c.probability_of_lef_rebind, c.min_n_of_loops_per_lef, c.min_n_of_burnin_rounds);
    absl::FPrintF(stderr, "Burnin completed in %s! (%lu rounds).\n",
                  absl::FormatDuration(absl::Now() - t0), burnin_rounds);
  }

  t0 = absl::Now();
  absl::FPrintF(stderr, "About to start simulating loop extrusion...\n");
  genome.simulate_extrusion(c.simulation_iterations);
  absl::FPrintF(stderr, "Simulation took %s.\n", absl::FormatDuration(absl::Now() - t0));

  if (!c.skip_output) { // Mostly useful for profiling
    genome.write_contacts_to_file(c.output_dir, c.force);
    genome.write_extrusion_barriers_to_file(c.output_dir, c.force);
    std::ofstream cmd_file(absl::StrFormat("%s/settings.log", c.output_dir));
    cmd_file << c.to_string() << std::endl;
    cmd_file << absl::StrJoin(c.argv, c.argv + c.argc, " ") << std::endl;
    cmd_file.close();

    if (c.make_heatmaps) {
      std::string_view exec(c.argv[0]);
      auto path_hint = exec.substr(0, exec.rfind('/'));
      std::string path_to_script = "modle_plot_contact_matrix.py";
      if (const auto p = absl::StrFormat("%s/%s", path_hint, path_to_script);
          std::filesystem::is_regular_file(p)) {
        path_to_script = p;
      }
      genome.make_heatmaps(c.output_dir, c.force, path_to_script);
    }
  }
  absl::FPrintF(stderr, "Simulation terminated without errors!\nBye.\n");
}
}  // namespace modle

int main(int argc, char** argv) {
  auto cli = modle::Cli(argc, argv);

  try {
    auto config = cli.parse_arguments();
    config.print();

    if (const auto files = check_if_output_file_exists(config); !files.empty()) {
      absl::FPrintF(
          stderr,
          "Refusing to run the simulation because some of the output file(s) already exist. Pass "
          "--force to overwrite.\nCollision detected for the following file(s):\n - %s\n",
          absl::StrJoin(files.begin(), files.end(), "\n - "));
      return 1;
    }

    modle::run_simulation(config);
  } catch (const CLI::ParseError& e) {
    return cli.exit(e);  //  This takes care of formatting and printing error messages (if any)
  }
  return 0;
}