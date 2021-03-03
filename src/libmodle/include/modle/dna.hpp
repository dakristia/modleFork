#pragma once

#include <absl/container/btree_set.h>
#include <absl/types/span.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "modle/bed.hpp"
#include "modle/chr_sizes.hpp"
#include "modle/common.hpp"
#include "modle/contacts.hpp"

namespace modle {

class Chromosome {
 public:
  inline explicit Chromosome(const chr_sizes::ChrSize& chrom,
                             const std::vector<bed::BED>& barriers = {});
  inline explicit Chromosome(chr_sizes::ChrSize&& chrom, std::vector<bed::BED>&& barriers = {});

  [[nodiscard]] inline bool operator==(const Chromosome& other) const;
  [[nodiscard]] inline bool operator==(std::string_view other) const;
  [[nodiscard]] inline bool operator<(const Chromosome& other) const;

  inline void instantiate_contact_matrix(std::size_t bin_size, std::size_t diagonal_width);
  inline void clear_contacts();
  inline void add_extrusion_barrier(bed::BED&& barrier);
  inline void add_extrusion_barrier(bed::BED barrier);

  [[nodiscard]] inline std::string_view name() const;
  [[nodiscard]] inline std::size_t start_pos() const;
  [[nodiscard]] inline std::size_t end_pos() const;
  [[nodiscard]] inline std::size_t size() const;
  [[nodiscard]] inline std::size_t simulated_size() const;
  [[nodiscard]] inline std::size_t nbarriers() const;
  [[nodiscard]] inline const absl::btree_set<bed::BED>& get_barriers() const;

  template <typename H>
  inline friend H AbslHashValue(H h, const Chromosome& c);

  struct Comparator {
    using is_transparent = void;
    [[nodiscard]] inline bool operator()(const Chromosome& c1, const Chromosome& c2) const;
    [[nodiscard]] inline bool operator()(const Chromosome& c1, std::string_view c2) const;
    [[nodiscard]] inline bool operator()(std::string_view c1, const Chromosome& c2) const;
    [[nodiscard]] inline bool operator()(std::string_view c1, std::string_view c2) const;
  };

 private:
  std::string _name;
  std::size_t _size;
  std::size_t _start;
  std::size_t _end;
  absl::btree_set<bed::BED> _barriers{};
  std::shared_ptr<ContactMatrix<Contacts>> _contacts{nullptr};
};

}  // namespace modle

#include "../../dna_impl.hpp"
