// IWYU pragma: private, include "modle/simulation.hpp"

#include "modle/simulation.hpp"

#include <H5Cpp.h>                        // IWYU pragma: keep
#include <absl/container/btree_set.h>     // for btree_iterator
#include <absl/types/span.h>              // for Span, MakeSpan, MakeConstSpan
#include <cpp-sort/sorter_facade.h>       // for sorter_facade
#include <cpp-sort/sorters/pdq_sorter.h>  // for pdq_sort, pdq_sorter
#include <fmt/format.h>                   // for format, print, FMT_STRING
#include <fmt/ostream.h>                  // for formatbuf<>::int_type

#include <algorithm>                                // for fill, min, max, clamp, for_each, gene...
#include <atomic>                                   // for atomic
#include <boost/asio/thread_pool.hpp>               // for thread_pool
#include <boost/dynamic_bitset/dynamic_bitset.hpp>  // for dynamic_bitset, dynamic_bitset<>::ref...
#include <boost/filesystem/path.hpp>                // for operator<<, path
#include <cassert>                                  // for assert
#include <chrono>                                   // for microseconds
#include <cmath>                                    // for round
#include <cstddef>                                  // for size_t
#include <cstdio>                                   // for stderr
#include <cstdlib>                                  // for abs
#include <deque>                                    // for deque
#include <ios>                                      // for streamsize
#include <limits>                                   // for numeric_limits
#include <memory>                                   // for unique_ptr, make_unique, _MakeUniq<>:...
#include <mutex>                                    // for mutex
#include <numeric>                                  // for iota
#include <stdexcept>                                // for runtime_error
#include <string>                                   // for basic_string, string
#include <string_view>                              // for string_view
#include <utility>                                  // for make_pair, pair
#include <vector>                                   // for vector, vector<>::iterator

#include "modle/common/common.hpp"  // for BOOST_LIKELY, BOOST_UNLIKELY bp_t...
#include "modle/common/config.hpp"  // for Config
#include "modle/common/genextreme_value_distribution.hpp"  // for genextreme_distribution
#include "modle/common/utils.hpp"                          // for ndebug_defined
#include "modle/cooler.hpp"                                // for Cooler, Cooler::WRITE_ONLY
#include "modle/extrusion_barriers.hpp"                    // for update_states, ExtrusionBarrier
#include "modle/extrusion_factors.hpp"                     // for Lef, ExtrusionUnit
#include "modle/genome.hpp"                                // for Chromosome, Genome

#ifndef BOOST_STACKTRACE_USE_NOOP
#include <boost/exception/get_error_info.hpp>  // for get_error_info
#include <boost/stacktrace/stacktrace.hpp>     // for operator<<
#include <ios>                                 // IWYU pragma: keep for streamsize
#endif

namespace modle {

Simulation::Simulation(const Config& c, bool import_chroms)
    : Config(c),
      _config(&c),
      _genome(import_chroms
                  ? Genome(path_to_chrom_sizes, path_to_extr_barriers, path_to_chrom_subranges,
                           path_to_feature_bed_files, ctcf_occupied_self_prob,
                           ctcf_not_occupied_self_prob, write_contacts_for_ko_chroms)
                  : Genome{}) {}

size_t Simulation::size() const { return this->_genome.size(); }

size_t Simulation::simulated_size() const { return this->_genome.simulated_size(); }

void Simulation::write_contacts_to_disk(std::deque<std::pair<Chromosome*, size_t>>& progress_queue,
                                        std::mutex& progress_queue_mutex,
                                        std::atomic<bool>& end_of_simulation)
    const {  // This thread is in charge of writing contacts to disk
  Chromosome* chrom_to_be_written = nullptr;
  const auto max_str_length =
      std::max_element(  // Find chrom with the longest name
          this->_genome.begin(), this->_genome.end(),
          [](const auto& c1, const auto& c2) { return c1.name().size() < c2.name().size(); })
          ->name()
          .size();

  auto c = this->skip_output ? nullptr
                             : std::make_unique<cooler::Cooler>(this->path_to_output_file_cool,
                                                                cooler::Cooler::WRITE_ONLY,
                                                                this->bin_size, max_str_length);

  auto sleep_us = 100;
  while (true) {  // Structuring the loop in this way allows us to sleep without holding the mutex
    sleep_us = std::min(500000, sleep_us * 2);  // NOLINT
    std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
    {
      std::scoped_lock l(progress_queue_mutex);
      if (progress_queue.empty()) {
        // There are no contacts to write to disk at the moment. Go back to sleep
        continue;
      }

      // chrom == nullptr is the end-of-queue signal
      if (auto& [chrom, count] = progress_queue.front(); chrom == nullptr) {
        end_of_simulation = true;
        return;
      }
      // count == ncells signals that we are done simulating the current chromosome
      else if (count == num_cells) {  // NOLINT
        chrom_to_be_written = chrom;
        progress_queue.pop_front();
      } else {
        assert(count < num_cells);  // NOLINT
        continue;
      }
    }
    sleep_us = 100;
    try {
      if (c) {  // c == nullptr only when --skip-output is used
        // NOTE here we have to use pointers instead of reference because
        // chrom_to_be_written.contacts() == nullptr is used to signal an empty matrix.
        // In this case, c->write_or_append_cmatrix_to_file() will create an entry in the chroms and
        // bins datasets, as well as update the appropriate index
        if (chrom_to_be_written->contacts_ptr()) {
          fmt::print(stderr, "Writing contacts for '{}' to file {}...\n",
                     chrom_to_be_written->name(), c->get_path());
        } else {
          fmt::print(stderr, "Creating an empty entry for '{}' in file {}...\n",
                     chrom_to_be_written->name(), c->get_path());
        }

        c->write_or_append_cmatrix_to_file(
            chrom_to_be_written->contacts_ptr(), chrom_to_be_written->name(),
            chrom_to_be_written->start_pos(), chrom_to_be_written->end_pos(),
            chrom_to_be_written->size(), true);

        if (chrom_to_be_written->contacts_ptr()) {
          fmt::print(
              stderr, "Written {} contacts for '{}' in {:.2f}M pixels to file {}.\n",
              chrom_to_be_written->contacts().get_tot_contacts(), chrom_to_be_written->name(),
              static_cast<double>(chrom_to_be_written->contacts().npixels()) / 1.0e6,  // NOLINT
              c->get_path());
        } else {
          fmt::print(stderr, "Created an entry for '{}' in file {}.\n", chrom_to_be_written->name(),
                     c->get_path());
        }
      }
      // Deallocate the contact matrix to free up unused memory
      chrom_to_be_written->deallocate_contacts();
    } catch (const std::runtime_error& err) {
      throw std::runtime_error(fmt::format(
          FMT_STRING("The following error occurred while writing contacts for '{}' to file {}: {}"),
          chrom_to_be_written->name(), c->get_path(), err.what()));
    }
  }
}

bp_t Simulation::generate_rev_move(const Chromosome& chrom, const ExtrusionUnit& unit,
                                   random::PRNG_t& rand_eng) const {
  assert(unit.pos() >= chrom.start_pos());   // NOLINT
  if (this->rev_extrusion_speed_std == 0) {  // When std == 0 always return the avg. extrusion speed
    // (except when unit is close to chrom start pos.)
    return std::min(this->rev_extrusion_speed, unit.pos() - chrom.start_pos());
  }
  // Generate the move distance (and make sure it is not a negative distance)
  // NOTE: on my laptop generating doubles from a normal distribution, rounding them, then
  // casting double to uint is a lot faster (~4x) than drawing uints directly from a Poisson
  // distr.
  return std::clamp(static_cast<bp_t>(std::round(
                        lef_move_generator_t{static_cast<double>(this->rev_extrusion_speed),
                                             this->rev_extrusion_speed_std}(rand_eng))),
                    0UL, unit.pos() - chrom.start_pos());
}

bp_t Simulation::generate_fwd_move(const Chromosome& chrom, const ExtrusionUnit& unit,
                                   random::PRNG_t& rand_eng) const {
  // See Simulation::generate_rev_move for comments
  assert(unit.pos() < chrom.end_pos());  // NOLINT
  if (this->fwd_extrusion_speed_std == 0) {
    return std::min(this->fwd_extrusion_speed, (chrom.end_pos() - 1) - unit.pos());
  }
  return std::clamp(static_cast<bp_t>(std::round(
                        lef_move_generator_t{static_cast<double>(this->fwd_extrusion_speed),
                                             this->fwd_extrusion_speed_std}(rand_eng))),
                    0UL, (chrom.end_pos() - 1) - unit.pos());
}

void Simulation::generate_moves(const Chromosome& chrom, const absl::Span<const Lef> lefs,
                                const absl::Span<const size_t> rev_lef_ranks,
                                const absl::Span<const size_t> fwd_lef_ranks,
                                const absl::Span<bp_t> rev_moves, const absl::Span<bp_t> fwd_moves,
                                random::PRNG_t& rand_eng, bool adjust_moves_) const
    noexcept(utils::ndebug_defined()) {
  {
    assert(lefs.size() == fwd_lef_ranks.size());  // NOLINT
    assert(lefs.size() == rev_lef_ranks.size());  // NOLINT
    assert(lefs.size() == fwd_moves.size());      // NOLINT
    assert(lefs.size() == rev_moves.size());      // NOLINT
  }

  // As long as a LEF is bound to DNA, always generate a move
  for (auto i = 0UL; i < lefs.size(); ++i) {
    rev_moves[i] = lefs[i].is_bound() ? generate_rev_move(chrom, lefs[i].rev_unit, rand_eng) : 0UL;
    fwd_moves[i] = lefs[i].is_bound() ? generate_fwd_move(chrom, lefs[i].fwd_unit, rand_eng) : 0UL;
  }

  if (adjust_moves_) {  // Adjust moves of consecutive extr. units to make LEF behavior more
    // realistic See comments in adjust_moves_of_consecutive_extr_units for more
    // details on what this entails
    Simulation::adjust_moves_of_consecutive_extr_units(chrom, lefs, rev_lef_ranks, fwd_lef_ranks,
                                                       rev_moves, fwd_moves);
  }
}

void Simulation::adjust_moves_of_consecutive_extr_units(
    const Chromosome& chrom, absl::Span<const Lef> lefs, absl::Span<const size_t> rev_lef_ranks,
    absl::Span<const size_t> fwd_lef_ranks, absl::Span<bp_t> rev_moves,
    absl::Span<bp_t> fwd_moves) noexcept(utils::ndebug_defined()) {
  (void)chrom;

  // Loop over pairs of consecutive extr. units.
  // Extr. units moving in rev direction are processed in 3'-5' order, while units moving in fwd
  // direction are processed in 5'-3' direction
  const auto rev_offset = lefs.size() - 1;
  for (auto i = 0UL; i < lefs.size() - 1; ++i) {
    const auto& idx1 = rev_lef_ranks[rev_offset - 1 - i];
    const auto& idx2 = rev_lef_ranks[rev_offset - i];

    if (lefs[idx1].is_bound() && lefs[idx2].is_bound()) {
      assert(lefs[idx1].rev_unit.pos() >= chrom.start_pos() + rev_moves[idx1]);  // NOLINT
      assert(lefs[idx2].rev_unit.pos() >= chrom.start_pos() + rev_moves[idx2]);  // NOLINT

      const auto pos1 = lefs[idx1].rev_unit.pos() - rev_moves[idx1];
      const auto pos2 = lefs[idx2].rev_unit.pos() - rev_moves[idx2];

      // If moving extr. units 1 and 2 by their respective moves would cause unit 1 (which comes
      // first in 3'-5' order) to be surpassed by unit 2 (which comes second in 3'-5' order),
      // increment the move for unit 1 so that after moving both units, unit 1 will be located one
      // bp upstream of unit 2 in 5'-3' direction.
      // This mimics what would probably happen in a real system, where extr. unit 2 would most
      // likely push extr. unit 1, temporarily increasing extr. speed of unit 1.
      if (pos2 < pos1) {
        rev_moves[idx1] += pos1 - pos2;
      }
    }

    const auto& idx3 = fwd_lef_ranks[i];
    const auto& idx4 = fwd_lef_ranks[i + 1];

    // See above for detailed comments. The logic is the same used on rev units (but mirrored!)
    if (lefs[idx3].is_bound() && lefs[idx4].is_bound()) {
      assert(lefs[idx3].fwd_unit.pos() + fwd_moves[idx3] < chrom.end_pos());  // NOLINT
      assert(lefs[idx4].fwd_unit.pos() + fwd_moves[idx4] < chrom.end_pos());  // NOLINT

      const auto pos3 = lefs[idx3].fwd_unit.pos() + fwd_moves[idx3];
      const auto pos4 = lefs[idx4].fwd_unit.pos() + fwd_moves[idx4];

      if (pos3 > pos4) {
        fwd_moves[idx4] += pos3 - pos4;
      }
    }
  }
}

void Simulation::rank_lefs(const absl::Span<const Lef> lefs,
                           const absl::Span<size_t> rev_lef_rank_buff,
                           const absl::Span<size_t> fwd_lef_rank_buff,
                           bool ranks_are_partially_sorted,
                           bool init_buffers) noexcept(utils::ndebug_defined()) {
  assert(lefs.size() == fwd_lef_rank_buff.size());  // NOLINT
  assert(lefs.size() == rev_lef_rank_buff.size());  // NOLINT

  auto rev_comparator = [&](const auto r1, const auto r2) constexpr noexcept {
    assert(r1 < lefs.size());  // NOLINT
    assert(r2 < lefs.size());  // NOLINT
    return lefs[r1].rev_unit.pos() < lefs[r2].rev_unit.pos();
  };

  // See comments for rev_comparator.
  auto fwd_comparator = [&](const auto r1, const auto r2) constexpr noexcept {
    assert(r1 < lefs.size());  // NOLINT
    assert(r2 < lefs.size());  // NOLINT
    return lefs[r1].fwd_unit.pos() < lefs[r2].fwd_unit.pos();
  };

  if (BOOST_UNLIKELY(init_buffers)) {  // Init rank buffers
    std::iota(fwd_lef_rank_buff.begin(), fwd_lef_rank_buff.end(), 0);
    std::iota(rev_lef_rank_buff.begin(), rev_lef_rank_buff.end(), 0);
  }

  if (BOOST_LIKELY(ranks_are_partially_sorted)) {
    cppsort::split_sort(rev_lef_rank_buff.begin(), rev_lef_rank_buff.end(), rev_comparator);
    cppsort::split_sort(fwd_lef_rank_buff.begin(), fwd_lef_rank_buff.end(), fwd_comparator);
  } else {
    // Fallback to pattern-defeating quicksort we have no information regarding the level of
    // pre-sortedness of LEFs
    cppsort::pdq_sort(rev_lef_rank_buff.begin(), rev_lef_rank_buff.end(), rev_comparator);
    cppsort::pdq_sort(fwd_lef_rank_buff.begin(), fwd_lef_rank_buff.end(), fwd_comparator);
  }

  // TODO Figure out a better way to deal with ties
  auto begin = 0UL;
  auto end = 0UL;
  for (auto i = 1UL; i < rev_lef_rank_buff.size(); ++i) {
    const auto& r1 = rev_lef_rank_buff[i - 1];
    const auto& r2 = rev_lef_rank_buff[i];
    if (BOOST_UNLIKELY(lefs[r1].rev_unit.pos() == lefs[r2].rev_unit.pos())) {
      begin = i - 1;
      for (; i < rev_lef_rank_buff.size(); ++i) {
        const auto& r11 = rev_lef_rank_buff[i - 1];
        const auto& r22 = rev_lef_rank_buff[i];
        if (lefs[r11].rev_unit.pos() != lefs[r22].rev_unit.pos()) {
          break;
        }
      }

      end = i;
      cppsort::insertion_sort(rev_lef_rank_buff.begin() + begin, rev_lef_rank_buff.begin() + end,
                              [&lefs](const auto r11, const auto r22) {
                                assert(r11 < lefs.size());  // NOLINT
                                assert(r22 < lefs.size());  // NOLINT
                                return lefs[r11].binding_epoch < lefs[r22].binding_epoch;
                              });
      begin = end;
    }
  }

  for (auto i = 1UL; i < fwd_lef_rank_buff.size(); ++i) {
    const auto& r1 = fwd_lef_rank_buff[i - 1];
    const auto& r2 = fwd_lef_rank_buff[i];
    if (BOOST_UNLIKELY(lefs[r1].fwd_unit.pos() == lefs[r2].fwd_unit.pos())) {
      begin = i - 1;
      for (; i < fwd_lef_rank_buff.size(); ++i) {
        const auto& r11 = fwd_lef_rank_buff[i - 1];
        const auto& r22 = fwd_lef_rank_buff[i];
        if (lefs[r11].fwd_unit.pos() != lefs[r22].fwd_unit.pos()) {
          break;
        }
      }

      end = i;
      cppsort::insertion_sort(fwd_lef_rank_buff.begin() + begin, fwd_lef_rank_buff.begin() + end,
                              [&lefs](const auto r11, const auto r22) {
                                assert(r11 < lefs.size());  // NOLINT
                                assert(r22 < lefs.size());  // NOLINT
                                return lefs[r22].binding_epoch < lefs[r11].binding_epoch;
                              });
      begin = end;
    }
  }
}

void Simulation::extrude(const Chromosome& chrom, const absl::Span<Lef> lefs,
                         const absl::Span<const bp_t> rev_moves,
                         const absl::Span<const bp_t> fwd_moves,
                         const size_t num_rev_units_at_5prime,
                         const size_t num_fwd_units_at_3prime) noexcept(utils::ndebug_defined()) {
  {
    assert(lefs.size() == rev_moves.size());         // NOLINT
    assert(lefs.size() == fwd_moves.size());         // NOLINT
    assert(lefs.size() >= num_rev_units_at_5prime);  // NOLINT
    assert(lefs.size() >= num_fwd_units_at_3prime);  // NOLINT
    (void)chrom;
  }

  auto i1 = num_rev_units_at_5prime == 0 ? 0UL : num_rev_units_at_5prime - 1;
  const auto i2 = lefs.size() - num_fwd_units_at_3prime;
  for (; i1 < i2; ++i1) {
    auto& lef = lefs[i1];
    if (BOOST_UNLIKELY(!lef.is_bound())) {  // Do not process inactive LEFs
      continue;
    }
    assert(lef.rev_unit.pos() <= lef.fwd_unit.pos());                   // NOLINT
    assert(lef.rev_unit.pos() >= chrom.start_pos() + rev_moves[i1]);    // NOLINT
    assert(lef.fwd_unit.pos() + fwd_moves[i1] <= chrom.end_pos() - 1);  // NOLINT

    // Extrude rev unit
    lef.rev_unit._pos -= rev_moves[i1];  // Advance extr. unit in 3'-5' direction

    // Extrude fwd unit
    lef.fwd_unit._pos += fwd_moves[i1];                // Advance extr. unit in 5'-3' direction
    assert(lef.rev_unit.pos() <= lef.fwd_unit.pos());  // NOLINT
  }
}

std::pair<bp_t, bp_t> Simulation::compute_lef_lef_collision_pos(const ExtrusionUnit& rev_unit,
                                                                const ExtrusionUnit& fwd_unit,
                                                                bp_t rev_move, bp_t fwd_move) {
  const auto& rev_speed = rev_move;
  const auto& fwd_speed = fwd_move;
  const auto& rev_pos = rev_unit.pos();
  const auto& fwd_pos = fwd_unit.pos();

  const auto relative_speed = rev_speed + fwd_speed;
  const auto time_to_collision =
      static_cast<double>(rev_pos - fwd_pos) / static_cast<double>(relative_speed);

  const auto collision_pos =
      fwd_pos + static_cast<bp_t>(std::round((static_cast<double>(fwd_speed) * time_to_collision)));
  assert(collision_pos <= rev_pos);  // NOLINT
#ifndef NDEBUG
  const auto collision_pos_ =
      static_cast<double>(rev_pos) - static_cast<double>(rev_speed) * time_to_collision;
  assert(abs(static_cast<double>(collision_pos) - collision_pos_) < 1.0);  // NOLINT
#endif
  if (BOOST_UNLIKELY(collision_pos == fwd_pos)) {
    assert(collision_pos >= fwd_pos);      // NOLINT
    assert(collision_pos + 1 <= rev_pos);  // NOLINT
    return std::make_pair(collision_pos + 1, collision_pos);
  }
  assert(collision_pos > 0);             // NOLINT
  assert(collision_pos - 1 >= fwd_pos);  // NOLINT
  return std::make_pair(collision_pos, collision_pos - 1);
}

size_t Simulation::register_contacts(Chromosome& chrom, const absl::Span<const Lef> lefs,
                                     const absl::Span<const size_t> selected_lef_idx) const
    noexcept(utils::ndebug_defined()) {
  return this->register_contacts(chrom.start_pos() + 1, chrom.end_pos() - 1, chrom.contacts(), lefs,
                                 selected_lef_idx);
}

size_t Simulation::register_contacts(const bp_t start_pos, const bp_t end_pos,
                                     ContactMatrix<contacts_t>& contacts,
                                     const absl::Span<const Lef> lefs,
                                     const absl::Span<const size_t> selected_lef_idx) const
    noexcept(utils::ndebug_defined()) {
  // Register contacts for the selected LEFs (excluding LEFs that have one of their units at the
  // beginning/end of a chromosome)
  size_t new_contacts = 0;
  for (const auto i : selected_lef_idx) {
    assert(i < lefs.size());  // NOLINT
    const auto& lef = lefs[i];
    if (BOOST_LIKELY(lef.is_bound() && lef.rev_unit.pos() > start_pos &&
                     lef.rev_unit.pos() < end_pos && lef.fwd_unit.pos() > start_pos &&
                     lef.fwd_unit.pos() < end_pos)) {
      const auto pos1 = lef.rev_unit.pos() - start_pos;
      const auto pos2 = lef.fwd_unit.pos() - start_pos;
      contacts.increment(pos1 / this->bin_size, pos2 / this->bin_size);
      ++new_contacts;
    }
  }
  return new_contacts;
}

size_t Simulation::register_contacts_w_randomization(Chromosome& chrom, absl::Span<const Lef> lefs,
                                                     absl::Span<const size_t> selected_lef_idx,
                                                     random::PRNG_t& rand_eng) const
    noexcept(utils::ndebug_defined()) {
  return this->register_contacts_w_randomization(chrom.start_pos() + 1, chrom.end_pos() - 1,
                                                 chrom.contacts(), lefs, selected_lef_idx,
                                                 rand_eng);
}

size_t Simulation::register_contacts_w_randomization(bp_t start_pos, bp_t end_pos,
                                                     ContactMatrix<contacts_t>& contacts,
                                                     absl::Span<const Lef> lefs,
                                                     absl::Span<const size_t> selected_lef_idx,
                                                     random::PRNG_t& rand_eng) const
    noexcept(utils::ndebug_defined()) {
  auto noise_gen = genextreme_value_distribution<double>{
      this->genextreme_mu, this->genextreme_sigma, this->genextreme_xi};

  size_t new_contacts = 0;
  for (const auto i : selected_lef_idx) {
    assert(i < lefs.size());  // NOLINT
    const auto& lef = lefs[i];
    if (BOOST_LIKELY(lef.is_bound() && lef.rev_unit.pos() > start_pos &&
                     lef.rev_unit.pos() < end_pos && lef.fwd_unit.pos() > start_pos &&
                     lef.fwd_unit.pos() < end_pos)) {
      const auto p1 = static_cast<double>(lef.rev_unit.pos() - start_pos) - noise_gen(rand_eng);
      const auto p2 = static_cast<double>(lef.fwd_unit.pos() - start_pos) + noise_gen(rand_eng);

      if (p1 < 0 || p2 < 0 || p1 > static_cast<double>(end_pos) ||
          p2 > static_cast<double>(end_pos)) {
        continue;
      }

      const auto pos1 = static_cast<bp_t>(std::round(p1));
      const auto pos2 = static_cast<bp_t>(std::round(p2));
      contacts.increment(pos1 / this->bin_size, pos2 / this->bin_size);
      ++new_contacts;
    }
  }
  return new_contacts;
}

void Simulation::generate_lef_unloader_affinities(
    const absl::Span<const Lef> lefs, const absl::Span<const ExtrusionBarrier> barriers,
    const absl::Span<const collision_t> rev_collisions,
    const absl::Span<const collision_t> fwd_collisions,
    const absl::Span<double> lef_unloader_affinity) const noexcept(utils::ndebug_defined()) {
  assert(lefs.size() == rev_collisions.size());         // NOLINT
  assert(lefs.size() == fwd_collisions.size());         // NOLINT
  assert(lefs.size() == lef_unloader_affinity.size());  // NOLINT

  // Changing ii -> i raises a -Werror=shadow on GCC 7.5
  auto is_lef_bar_collision = [&](const auto ii) { return ii < barriers.size(); };

  for (auto i = 0UL; i < lefs.size(); ++i) {
    const auto& lef = lefs[i];
    if (!lef.is_bound()) {
      lef_unloader_affinity[i] = 0.0;
    } else if (BOOST_LIKELY(!is_lef_bar_collision(rev_collisions[i]) ||
                            !is_lef_bar_collision(fwd_collisions[i]))) {
      lef_unloader_affinity[i] = 1.0;
    } else {
      const auto& rev_barrier = barriers[rev_collisions[i]];
      const auto& fwd_barrier = barriers[fwd_collisions[i]];

      if (BOOST_UNLIKELY(rev_barrier.blocking_direction_major() == dna::rev &&
                         fwd_barrier.blocking_direction_major() == dna::fwd)) {
        lef_unloader_affinity[i] = 1.0 / this->hard_stall_multiplier;
      } else {
        lef_unloader_affinity[i] = 1.0;
      }
    }
  }
}

void Simulation::select_lefs_to_release(
    const absl::Span<size_t> lef_idx, const absl::Span<const double> lef_unloader_affinity,
    random::PRNG_t& rand_eng) noexcept(utils::ndebug_defined()) {
  random::discrete_distribution<size_t> idx_gen(lef_unloader_affinity.begin(),
                                                lef_unloader_affinity.end());
  std::generate(lef_idx.begin(), lef_idx.end(), [&]() { return idx_gen(rand_eng); });
}

void Simulation::release_lefs(const absl::Span<Lef> lefs,
                              const absl::Span<const size_t> lef_idx) noexcept {
  for (const auto i : lef_idx) {
    assert(i < lefs.size());  // NOLINT
    lefs[i].release();
  }
}

boost::asio::thread_pool Simulation::instantiate_thread_pool() const {
  return boost::asio::thread_pool(this->nthreads);
}

Simulation::State& Simulation::State::operator=(const Task& task) {
  this->id = task.id;
  this->chrom = task.chrom;
  this->cell_id = task.cell_id;
  this->num_target_epochs = task.num_target_epochs;
  this->num_target_contacts = task.num_target_contacts;
  this->num_lefs = task.num_lefs;
  this->barriers = task.barriers;
  return *this;
}

void Simulation::BaseState::_resize_buffers(const size_t new_size) {
  lef_buff.resize(new_size);
  lef_unloader_affinity.resize(new_size);
  rank_buff1.resize(new_size);
  rank_buff2.resize(new_size);
  moves_buff1.resize(new_size);
  moves_buff2.resize(new_size);
  idx_buff.resize(new_size);
  collision_buff1.resize(new_size);
  collision_buff2.resize(new_size);
  epoch_buff.resize(new_size);
}

void Simulation::BaseState::_reset_buffers() {  // TODO figure out which resets are redundant
  std::for_each(lef_buff.begin(), lef_buff.end(), [](auto& lef) { lef.reset(); });
  std::fill(lef_unloader_affinity.begin(), lef_unloader_affinity.end(), 0.0);
  std::iota(rank_buff1.begin(), rank_buff1.end(), 0);
  std::copy(rank_buff1.begin(), rank_buff1.end(), rank_buff2.begin());
  barrier_mask.reset();
  std::fill(moves_buff1.begin(), moves_buff1.end(), 0);
  std::fill(moves_buff2.begin(), moves_buff2.end(), 0);
  std::fill(collision_buff1.begin(), collision_buff1.end(), 0);
  std::fill(collision_buff2.begin(), collision_buff2.end(), 0);
  std::fill(epoch_buff.begin(), epoch_buff.end(), 0);
}

void Simulation::State::resize_buffers(size_t new_size) {
  if (new_size == std::numeric_limits<size_t>::max()) {
    new_size = this->num_lefs;
  }
  this->_resize_buffers(new_size);
  barrier_mask.resize(this->barriers.size());
}

void Simulation::State::reset_buffers() { this->_reset_buffers(); }

std::string Simulation::State::to_string() const noexcept {
  return fmt::format(FMT_STRING("State:\n - TaskID {}\n"
                                " - Chrom: {}[{}-{}]\n"
                                " - CellID: {}\n"
                                " - Target epochs: {}\n"
                                " - Target contacts: {}\n"
                                " - # of LEFs: {}\n"
                                " - # Extrusion barriers: {}\n"
                                " - seed: {}\n"),
                     id, chrom->name(), chrom->start_pos(), chrom->end_pos(), cell_id,
                     num_target_epochs, num_target_contacts, num_lefs, barriers.size(), seed);
}

Simulation::StatePW& Simulation::StatePW::operator=(const TaskPW& task) {
  this->id = task.id;
  this->chrom = task.chrom;
  this->cell_id = task.cell_id;
  this->num_target_epochs = task.num_target_epochs;
  this->num_target_contacts = task.num_target_contacts;
  this->num_lefs = task.num_lefs;
  this->barriers = task.barriers;

  this->deletion_begin = task.deletion_begin;
  this->deletion_size = task.deletion_size;
  this->window_start = task.window_start;
  this->window_end = task.window_end;
  this->active_window_start = task.active_window_start;
  this->active_window_end = task.active_window_end;
  this->write_contacts_to_disk = task.write_contacts_to_disk;

  this->feats1 = task.feats1;
  this->feats2 = task.feats2;
  return *this;
}

void Simulation::StatePW::resize_buffers(size_t new_size) {
  if (new_size == std::numeric_limits<size_t>::max()) {
    new_size = this->num_lefs;
  }
  this->_resize_buffers(new_size);
  barrier_mask.resize(this->barriers.size());
}

void Simulation::StatePW::reset_buffers() { this->_reset_buffers(); }

std::string Simulation::StatePW::to_string() const noexcept {
  return fmt::format(FMT_STRING("StatePW:\n - TaskID {}\n"
                                " - Chrom: {}[{}-{}]\n"
                                " - Range start: {}\n"
                                " - Range end: {}\n"
                                " - CellID: {}\n"
                                " - Target epochs: {}\n"
                                " - Target contacts: {}\n"
                                " - # of LEFs: {}\n"
                                " - # Extrusion barriers: {}\n"
                                " - seed: {}\n"),
                     id, chrom->name(), chrom->start_pos(), chrom->end_pos(), window_start,
                     window_end, cell_id, num_target_epochs, num_target_contacts, num_lefs,
                     barriers.size(), seed);
}

std::pair<size_t, size_t> Simulation::process_collisions(
    const Chromosome& chrom, const absl::Span<const Lef> lefs,
    const absl::Span<const ExtrusionBarrier> barriers, const boost::dynamic_bitset<>& barrier_mask,
    const absl::Span<const size_t> rev_lef_ranks, const absl::Span<const size_t> fwd_lef_ranks,
    const absl::Span<bp_t> rev_moves, const absl::Span<bp_t> fwd_moves,
    const absl::Span<collision_t> rev_collisions, const absl::Span<collision_t> fwd_collisions,
    random::PRNG_t& rand_eng) const noexcept(utils::ndebug_defined()) {
  const auto& [num_rev_units_at_5prime, num_fwd_units_at_3prime] =
      Simulation::detect_units_at_chrom_boundaries(chrom, lefs, rev_lef_ranks, fwd_lef_ranks,
                                                   rev_moves, fwd_moves, rev_collisions,
                                                   fwd_collisions);

  this->detect_lef_bar_collisions(lefs, rev_lef_ranks, fwd_lef_ranks, rev_moves, fwd_moves,
                                  barriers, barrier_mask, rev_collisions, fwd_collisions, rand_eng,
                                  num_rev_units_at_5prime, num_fwd_units_at_3prime);

  this->detect_primary_lef_lef_collisions(lefs, barriers, rev_lef_ranks, fwd_lef_ranks, rev_moves,
                                          fwd_moves, rev_collisions, fwd_collisions, rand_eng,
                                          num_rev_units_at_5prime, num_fwd_units_at_3prime);
  Simulation::correct_moves_for_lef_bar_collisions(lefs, barriers, rev_moves, fwd_moves,
                                                   rev_collisions, fwd_collisions);

  Simulation::correct_moves_for_primary_lef_lef_collisions(lefs, barriers, rev_lef_ranks,
                                                           fwd_lef_ranks, rev_moves, fwd_moves,
                                                           rev_collisions, fwd_collisions);
  this->process_secondary_lef_lef_collisions(
      chrom, lefs, barriers.size(), rev_lef_ranks, fwd_lef_ranks, rev_moves, fwd_moves,
      rev_collisions, fwd_collisions, rand_eng, num_rev_units_at_5prime, num_fwd_units_at_3prime);
  return std::make_pair(num_rev_units_at_5prime, num_fwd_units_at_3prime);
}

absl::Span<const size_t> Simulation::setup_burnin(BaseState& s) const {
  // Generate the epoch at which each LEF is supposed to be initially loaded
  auto lef_initial_loading_epoch = absl::MakeSpan(s.epoch_buff);
  // lef_initial_loading_epoch.resize(this->skip_burnin ? 0 : s.nlefs);

  if (!skip_burnin) {
    // TODO Consider using a Poisson process instead of sampling from an uniform distribution
    random::uniform_int_distribution<size_t> round_gen{0, (4 * average_lef_lifetime) / bin_size};
    std::generate(lef_initial_loading_epoch.begin(), lef_initial_loading_epoch.end(),
                  [&]() { return round_gen(s.rand_eng); });

    // Sort epochs in descending order
    if (round_gen.max() > 2048) {
      // Counting sort uses n + r space in memory, where r is the number of unique values in the
      // range to be sorted. For this reason it is not a good idea to use it when the sampling
      // interval is relatively large. Whether 2048 is a reasonable threshold has yet to be tested
      cppsort::ska_sort(lef_initial_loading_epoch.rbegin(), lef_initial_loading_epoch.rend());
    } else {
      cppsort::counting_sort(lef_initial_loading_epoch.rbegin(), lef_initial_loading_epoch.rend());
    }
  }

  // Shift epochs so that the first epoch == 0
  if (const auto offset = lef_initial_loading_epoch.back(); offset != 0) {
    std::for_each(lef_initial_loading_epoch.begin(), lef_initial_loading_epoch.end(),
                  [&](auto& n) { n -= offset; });
  }
  return lef_initial_loading_epoch;
}

}  // namespace modle
