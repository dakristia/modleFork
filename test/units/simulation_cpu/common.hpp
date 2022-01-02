// Copyright (C) 2021 Roberto Rossini <roberros@uio.no>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <absl/types/span.h>
#include <fmt/format.h>

#include <algorithm>
#include <catch2/catch.hpp>
#include <cstddef>
#include <vector>

#include "modle/common/common.hpp"
#include "modle/extrusion_factors.hpp"
#include "modle/simulation.hpp"

namespace modle::test::libmodle {

using CollisionT = Collision<u32f>;
constexpr auto NO_COLLISION = CollisionT::NO_COLLISION;
constexpr auto COLLISION = CollisionT::COLLISION;
constexpr auto CHROM_BOUNDARY = COLLISION | CollisionT::CHROM_BOUNDARY;
constexpr auto LEF_BAR = COLLISION | CollisionT::LEF_BAR;
constexpr auto LEF_LEF_PRIMARY = COLLISION | CollisionT::LEF_LEF_PRIMARY;
constexpr auto LEF_LEF_SECONDARY = COLLISION | CollisionT::LEF_LEF_SECONDARY;

constexpr auto C = COLLISION;

template <typename I>
[[nodiscard]] inline Lef construct_lef(I p1, I p2, usize binding_epoch = 0) {
  return Lef{binding_epoch, ExtrusionUnit{static_cast<bp_t>(p1)},
             ExtrusionUnit{static_cast<bp_t>(p2)}};
}

[[maybe_unused]] inline void print_debug_info(
    usize i, const std::vector<bp_t>& rev_moves, const std::vector<bp_t>& fwd_moves,
    const std::vector<bp_t>& rev_moves_expected, const std::vector<bp_t>& fwd_moves_expected,
    const std::vector<CollisionT>& rev_collisions,
    const std::vector<CollisionT>& rev_collisions_expected,
    const std::vector<CollisionT>& fwd_collisions,
    const std::vector<CollisionT>& fwd_collisions_expected) {
  fmt::print(stderr, FMT_STRING("i={}; rev_move={}/{}; fwd_move={}/{};\n"), i, rev_moves[i],
             rev_moves_expected[i], fwd_moves[i], fwd_moves_expected[i]);
  fmt::print(
      stderr,
      FMT_STRING("i={}; rev_status: expected {:l} got {:l}]; fwd_status: expected {:l} got {:l}\n"),
      i, rev_collisions[i], rev_collisions_expected[i], fwd_collisions[i],
      fwd_collisions_expected[i]);
}

[[maybe_unused]] inline void print_debug_info(
    usize i, const std::vector<CollisionT>& rev_collisions,
    const std::vector<CollisionT>& rev_collisions_expected,
    const std::vector<CollisionT>& fwd_collisions,
    const std::vector<CollisionT>& fwd_collisions_expected) {
  fmt::print(stderr, FMT_STRING("i={}; rev_status=[{:l}\t{:l}]; fwd_status=[{:l}\t{:l}];\n"), i,
             rev_collisions[i], rev_collisions_expected[i], fwd_collisions[i],
             fwd_collisions_expected[i]);
}

[[maybe_unused]] inline void check_simulation_result(
    const std::vector<Lef>& lefs, const std::vector<bp_t>& rev_moves,
    const std::vector<bp_t>& fwd_moves, const std::vector<bp_t>& rev_moves_expected,
    const std::vector<bp_t>& fwd_moves_expected, const std::vector<CollisionT>& rev_collisions,
    const std::vector<CollisionT>& rev_collisions_expected,
    const std::vector<CollisionT>& fwd_collisions,
    const std::vector<CollisionT>& fwd_collisions_expected, bool print_debug_info_ = false) {
  for (usize i = 0; i < lefs.size(); ++i) {
    CHECK(rev_collisions[i] == rev_collisions_expected[i]);
    CHECK(fwd_collisions[i] == fwd_collisions_expected[i]);
    CHECK(rev_moves[i] == rev_moves_expected[i]);
    CHECK(fwd_moves[i] == fwd_moves_expected[i]);

    if (print_debug_info_) {
      print_debug_info(i, rev_moves, fwd_moves, rev_moves_expected, fwd_moves_expected,
                       rev_collisions, rev_collisions_expected, fwd_collisions,
                       fwd_collisions_expected);
    }
  }
}

[[maybe_unused]] inline void check_collisions(
    const std::vector<Lef>& lefs, const std::vector<CollisionT>& rev_collisions,
    const std::vector<CollisionT>& rev_collisions_expected,
    const std::vector<CollisionT>& fwd_collisions,
    const std::vector<CollisionT>& fwd_collisions_expected, bool print_debug_info_ = false) {
  for (usize i = 0; i < lefs.size(); ++i) {
    CHECK(rev_collisions[i] == rev_collisions_expected[i]);
    CHECK(fwd_collisions[i] == fwd_collisions_expected[i]);

    if (print_debug_info_) {
      print_debug_info(i, rev_collisions, rev_collisions_expected, fwd_collisions,
                       fwd_collisions_expected);
    }
  }
}

/*
[[maybe_unused]] inline void check_moves(
    const std::vector<Lef>& lefs, const std::vector<bp_t>& rev_moves,
    const std::vector<bp_t>& fwd_moves, const std::vector<bp_t>& rev_moves_expected,
    const std::vector<bp_t>& fwd_moves_expected, const std::vector<CollisionT>& rev_collisions,
    const std::vector<CollisionT>& rev_collisions_expected,
    const std::vector<CollisionT>& fwd_collisions,
    const std::vector<CollisionT>& fwd_collisions_expected, bool print_debug_info_ = false) {
  for (usize i = 0; i < lefs.size(); ++i) {
    CHECK(rev_collisions[i] == rev_collisions_expected[i]);
    CHECK(fwd_collisions[i] == fwd_collisions_expected[i]);
    CHECK(rev_moves[i] == rev_moves_expected[i]);
    CHECK(fwd_moves[i] == fwd_moves_expected[i]);

    if (print_debug_info_) {
      print_debug_info(i, rev_moves, fwd_moves, rev_moves_expected, fwd_moves_expected,
                       rev_collisions, rev_collisions_expected, fwd_collisions,
                       fwd_collisions_expected);
    }
  }
}
 */

inline void check_that_lefs_are_sorted_by_idx(const std::vector<Lef>& lefs,
                                              const std::vector<usize>& rev_ranks,
                                              const std::vector<usize>& fwd_ranks) {
  CHECK(std::is_sorted(fwd_ranks.begin(), fwd_ranks.end(), [&](const auto r1, const auto r2) {
    return lefs[r1].fwd_unit.pos() < lefs[r2].fwd_unit.pos();
  }));
  CHECK(std::is_sorted(rev_ranks.begin(), rev_ranks.end(), [&](const auto r1, const auto r2) {
    return lefs[r1].rev_unit.pos() < lefs[r2].rev_unit.pos();
  }));
}

inline void require_that_lefs_are_sorted_by_idx(const std::vector<Lef>& lefs,
                                                const std::vector<usize>& rev_ranks,
                                                const std::vector<usize>& fwd_ranks) {
  REQUIRE(std::is_sorted(fwd_ranks.begin(), fwd_ranks.end(), [&](const auto r1, const auto r2) {
    return lefs[r1].fwd_unit.pos() < lefs[r2].fwd_unit.pos();
  }));
  REQUIRE(std::is_sorted(rev_ranks.begin(), rev_ranks.end(), [&](const auto r1, const auto r2) {
    return lefs[r1].rev_unit.pos() < lefs[r2].rev_unit.pos();
  }));
}

[[nodiscard]] inline modle::Config init_config(usize rev_extrusion_speed = 3,
                                               usize fwd_extrusion_speed = 2,
                                               double rev_extrusion_speed_std = 0,
                                               double fwd_extrusion_speed_std = 0) noexcept {
  modle::Config c;

  c.rev_extrusion_speed = rev_extrusion_speed;
  c.rev_extrusion_speed_std = rev_extrusion_speed_std;
  c.fwd_extrusion_speed = fwd_extrusion_speed;
  c.fwd_extrusion_speed_std = fwd_extrusion_speed_std;
  c.probability_of_extrusion_unit_bypass = 0;

  return c;
}

[[nodiscard]] inline Chromosome init_chromosome(
    std::string_view name, bp_t chrom_size, bp_t chrom_start = 0,
    bp_t chrom_end = (std::numeric_limits<bp_t>::max)()) {
  chrom_end = std::min(chrom_end, chrom_size);
  assert(chrom_start < chrom_end);  // NOLINT
  return {0, name, chrom_start, chrom_end, chrom_size};
}
}  // namespace modle::test::libmodle
