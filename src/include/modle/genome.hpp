#ifndef MODLE_GENOME_HPP
#define MODLE_GENOME_HPP

#include <memory>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "modle/dna.hpp"
#include "modle/lefs.hpp"

namespace modle {
class Genome {
 public:
  struct Chromosome {
    Chromosome(std::string name, DNA dna);
    [[nodiscard]] uint32_t length() const;
    [[nodiscard]] uint32_t nbins() const;
    std::string name;
    DNA dna;
  };

  Genome(std::string_view path_to_bed, uint32_t bin_size, uint32_t n_lefs, uint32_t n_barriers,
         uint32_t avg_lef_processivity, double probability_of_barrier_block, uint64_t seed = 0);
  [[nodiscard]] const std::vector<Genome::Chromosome>& get_chromosomes() const;
  [[nodiscard]] std::vector<Genome::Chromosome>& get_chromosomes();
  [[nodiscard]] std::vector<Lef>& get_lefs();
  [[nodiscard]] const std::vector<Lef>& get_lefs() const;
  [[nodiscard]] absl::btree_multimap<std::string_view, std::shared_ptr<ExtrusionBarrier>>
  get_barriers();
  [[nodiscard]] uint32_t get_n_chromosomes() const;
  [[nodiscard]] uint64_t size() const;
  [[nodiscard]] uint32_t n_bins() const;
  [[nodiscard]] uint32_t tot_n_lefs() const;
  [[nodiscard]] uint32_t n_barriers() const;
  [[nodiscard]] std::vector<uint32_t> get_chromosome_lengths() const;

  void randomly_bind_lefs();
  uint32_t bind_free_lefs();
  void simulate_extrusion();

 private:
  std::string _path_to_bed;
  uint32_t _bin_size;
  std::vector<Chromosome> _chromosomes;
  std::default_random_engine _rndev;
  std::vector<Lef> _lefs;
  absl::flat_hash_map<std::string_view, std::shared_ptr<Lef>> _available_lefs{};
  uint32_t _avg_lef_processivity;
  absl::btree_multimap<std::string_view, std::shared_ptr<ExtrusionBarrier>> _barriers;
  double _probability_of_barrier_block;
  uint64_t _seed;

  [[nodiscard]] std::vector<Chromosome> init_chromosomes_from_bed() const;
  [[nodiscard]] static std::vector<Lef> generate_lefs(uint32_t n, uint32_t avg_processivity);
  [[nodiscard]] absl::btree_multimap<std::string_view, std::shared_ptr<ExtrusionBarrier>>
  randomly_generate_barriers(uint32_t n_barriers, double prob_of_block);
};
}  // namespace modle

#endif  // MODLE_GENOME_HPP
