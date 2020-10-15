#include "modle/extr_barrier.hpp"

#include "absl/strings/str_format.h"

namespace modle {
ExtrusionBarrier::ExtrusionBarrier(uint64_t pos, double prob_of_block, DNA::Direction direction)
    : _pos(pos), _direction(direction), _n_stall_generator(1 - prob_of_block) {}

uint64_t ExtrusionBarrier::get_pos() const { return this->_pos; }

double ExtrusionBarrier::get_prob_of_block() const { return this->_n_stall_generator.p(); }

DNA::Direction ExtrusionBarrier::get_direction() const { return this->_direction; }

uint32_t ExtrusionBarrier::generate_num_stalls(std::mt19937 &rand_dev) {
  return this->_n_stall_generator(rand_dev);
}

bool ExtrusionBarrier::operator<(const ExtrusionBarrier &other) const {
  return this->_pos < other._pos;
}

}  // namespace modle
