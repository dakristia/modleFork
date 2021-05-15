#include <cuda_runtime.h>
#include <curand_kernel.h>
#include <thrust/execution_policy.h>
#include <thrust/fill.h>

#include <cstdio>

#include "modle/cu/simulation.hpp"
#include "modle/cu/simulation_internal.hpp"

namespace modle::cu::kernels {

__global__ void init_curand(GlobalStateDev* global_state) {
  const auto seed = global_state->tasks[blockIdx.x].seed;
  curandStatePhilox4_32_10_t rng_state;

  curand_init(seed, threadIdx.x, 0, &rng_state);

  global_state->block_states[blockIdx.x].rng_state[threadIdx.x] = rng_state;
}

__global__ void reset_buffers(GlobalStateDev* global_state) {
  const auto bid = blockIdx.x;
  const auto tid = threadIdx.x;

  const auto nlefs = global_state->tasks[bid].nlefs;

  auto& block_state = global_state->block_states[bid];

  if (threadIdx.x == 0) {
    thrust::fill_n(thrust::device, block_state.rev_unit_pos, nlefs,
                   static_cast<int>(Simulation::LEF_IS_IDLE));
    thrust::fill_n(thrust::device, block_state.fwd_unit_pos, nlefs,
                   static_cast<int>(Simulation::LEF_IS_IDLE));
    block_state.num_active_lefs = 0;
    block_state.burnin_completed = false;
    block_state.simulation_completed = false;
    block_state.contact_local_buff_size = 0;
  }
  __syncthreads();

  const auto chunk_size = (nlefs + blockDim.x - 1) / blockDim.x;
  const auto i0 = tid * chunk_size;
  const auto i1 = min(i0 + chunk_size, nlefs);
  for (auto i = i0; i < i1; ++i) {
    block_state.lef_rev_unit_idx[i] = i;
    block_state.lef_fwd_unit_idx[i] = i;
  }
}

__global__ void generate_initial_loading_epochs(GlobalStateDev* global_state, uint32_t num_epochs) {
  const auto tid = threadIdx.x;
  const auto bid = blockIdx.x;
  const auto id = tid + bid * blockDim.x;
  const auto nthreads = blockDim.x * gridDim.x;

  auto* block_state = global_state->block_states + bid;
  const auto scaling_factor = static_cast<float>(4 * global_state->config->average_lef_lifetime) /
                              static_cast<float>(global_state->config->bin_size);
  auto rng_state_local = block_state->rng_state[tid];

  auto* global_buff = global_state->large_uint_buff1;

  const auto chunk_size = (num_epochs + nthreads - 1) / nthreads;
  const auto i0 = id * chunk_size;
  const auto i1 = min(i0 + chunk_size, num_epochs);

  for (auto i = i0; i < i1; ++i) {
    global_buff[i] = __float2uint_rn(curand_uniform(&rng_state_local) * scaling_factor);
  }

  block_state->rng_state[tid] = rng_state_local;
}

__global__ void init_barrier_states(GlobalStateDev* global_state) {
  const auto bid = blockIdx.x;

  const auto barrier_occupancy = global_state->config->probability_of_extrusion_barrier_block;
  auto local_rng_state = global_state->block_states[bid].rng_state[threadIdx.x];

  const auto nbarriers = global_state->tasks[bid].nbarriers;
  auto* barrier_states = global_state->block_states[bid].barrier_mask;

  const auto chunk_size = (nbarriers + blockDim.x - 1) / blockDim.x;
  const auto i0 = threadIdx.x * chunk_size;
  const auto i1 = min(i0 + chunk_size, nbarriers);

  for (auto i = i0; i < i1; ++i) {
    barrier_states[i] = curand_uniform(&local_rng_state) > 1.0F - barrier_occupancy
                            ? CTCF::OCCUPIED
                            : CTCF::NOT_OCCUPIED;
  }

  global_state->block_states[bid].rng_state[threadIdx.x] = local_rng_state;
}

__global__ void shift_and_scatter_lef_loading_epochs(const uint32_t* global_buff,
                                                     BlockState* block_states,
                                                     const uint32_t* begin_offsets,
                                                     const uint32_t* end_offsets) {
  const auto tid = threadIdx.x;
  const auto bid = blockIdx.x;

  const auto* source_ptr = global_buff + begin_offsets[bid];
  auto* dest_ptr = block_states[bid].epoch_buff;
  const auto size = end_offsets[bid] - begin_offsets[bid];

  if (threadIdx.x == 0) {
    memcpy(dest_ptr, source_ptr, size * sizeof(uint32_t));
  }

  __syncthreads();
  const auto offset = *source_ptr;
  const auto chunk_size = (size + blockDim.x - 1) / blockDim.x;
  const auto i0 = tid * chunk_size;
  const auto i1 = min(i0 + chunk_size, size);

  for (auto i = i0; i < i1; ++i) {
    dest_ptr[i] -= offset;
  }
}

__global__ void select_and_bind_lefs(uint32_t current_epoch, GlobalStateDev* global_state) {
  if (global_state->block_states[blockIdx.x].simulation_completed) {
    return;
  }
  const auto bid = blockIdx.x;
  const auto tid = threadIdx.x;

  const auto chrom_start = global_state->tasks[bid].chrom_start;
  const auto chrom_end = global_state->tasks[bid].chrom_end;
  const auto chrom_simulated_size = __uint2float_rn(chrom_end - chrom_start - 1);

  auto local_rng_state = global_state->block_states[bid].rng_state[tid];

  if (!global_state->block_states[bid].burnin_completed && tid == 0) {
    const auto nlefs = global_state->tasks[bid].nlefs;
    auto num_active_lefs = global_state->block_states[bid].num_active_lefs;
    do {
      if (global_state->block_states[bid].epoch_buff[num_active_lefs] > current_epoch) {
        break;
      }
    } while (++num_active_lefs < nlefs);
    global_state->block_states[bid].num_active_lefs = num_active_lefs;
    if (nlefs == num_active_lefs) {
      global_state->block_states[bid].burnin_completed = true;
    }
  }
  __syncthreads();

  auto* rev_unit_pos = global_state->block_states[bid].rev_unit_pos;
  auto* fwd_unit_pos = global_state->block_states[bid].fwd_unit_pos;

  auto* rev_unit_idx = global_state->block_states[bid].lef_rev_unit_idx;

  const auto num_active_lefs = global_state->block_states[bid].num_active_lefs;

  const auto chunk_size = (num_active_lefs + blockDim.x - 1) / blockDim.x;
  const auto i0 = tid * chunk_size;
  const auto i1 = min(i0 + chunk_size, num_active_lefs);

  for (auto i = i0; i < i1; ++i) {
    if (rev_unit_pos[i] == Simulation::LEF_IS_IDLE) {
      rev_unit_pos[i] =
          __float2uint_rn(roundf(curand_uniform(&local_rng_state) * chrom_simulated_size));
      const auto j = rev_unit_idx[i];

      assert(fwd_unit_pos[j] == Simulation::LEF_IS_IDLE);  // NOLINT
      fwd_unit_pos[j] = rev_unit_pos[i];
    }
  }
  global_state->block_states[bid].rng_state[tid] = local_rng_state;
}

__global__ void prepare_extr_units_for_sorting(GlobalStateDev* global_state,
                                               dna::Direction direction,
                                               uint32_t* tot_num_units_to_sort) {
  assert(direction == dna::Direction::fwd || direction == dna::Direction::rev);  // NOLINT
  if (global_state->block_states[blockIdx.x].simulation_completed) {
    return;
  }

  if (threadIdx.x == 0) {
    const auto bid = blockIdx.x;

    const auto buff_alignment = global_state->large_uint_buff_chunk_alignment;
    auto* buff1 = global_state->large_uint_buff1 + (bid * buff_alignment);
    auto* buff2 = global_state->large_uint_buff3 + (bid * buff_alignment);
    auto* start_offsets = global_state->sorting_offset1_buff;
    auto* end_offsets = global_state->sorting_offset2_buff;

    const auto num_active_lefs = global_state->block_states[bid].num_active_lefs;

    if (direction == dna::Direction::rev) {
      memcpy(buff1, global_state->block_states[bid].rev_unit_pos, num_active_lefs * sizeof(bp_t));
      memcpy(buff2, global_state->block_states[bid].lef_rev_unit_idx,
             num_active_lefs * sizeof(uint32_t));
    } else {
      memcpy(buff1, global_state->block_states[bid].fwd_unit_pos, num_active_lefs * sizeof(bp_t));
      memcpy(buff2, global_state->block_states[bid].lef_fwd_unit_idx,
             num_active_lefs * sizeof(uint32_t));
    }

    start_offsets[bid] = buff_alignment * bid;
    end_offsets[bid] = (buff_alignment * bid) + num_active_lefs;
    if (tot_num_units_to_sort) {
      atomicAdd(tot_num_units_to_sort, num_active_lefs);
    }
  }
}

__global__ void update_unit_mappings_and_scatter_sorted_lefs(
    GlobalStateDev* global_state, dna::Direction direction, bool update_extr_unit_to_lef_mappings) {
  if (global_state->block_states[blockIdx.x].simulation_completed) {
    return;
  }

  const auto tid = threadIdx.x;
  const auto bid = blockIdx.x;

  auto* block_state = global_state->block_states + bid;

  const auto* source_ptr = global_state->large_uint_buff1 + global_state->sorting_offset1_buff[bid];
  auto* dest_ptr =
      direction == dna::Direction::rev ? block_state->rev_unit_pos : block_state->fwd_unit_pos;
  const auto size = block_state->num_active_lefs;

  if (threadIdx.x == 0) {
    memcpy(dest_ptr, source_ptr, size * sizeof(uint32_t));
  }

  __syncthreads();
  if (update_extr_unit_to_lef_mappings) {
    const auto* source_idx = direction == dna::Direction::rev ? block_state->lef_rev_unit_idx
                                                              : block_state->lef_fwd_unit_idx;
    auto* dest_idx = direction == dna::Direction::rev ? block_state->lef_fwd_unit_idx
                                                      : block_state->lef_rev_unit_idx;

    const auto chunk_size = (size + blockDim.x - 1) / blockDim.x;
    const auto i0 = tid * chunk_size;
    const auto i1 = min(i0 + chunk_size, size);

    for (auto i = i0; i < i1; ++i) {
      const auto j = source_idx[i];
      dest_idx[j] = i;
    }
  }
}

__global__ void prepare_units_for_random_shuffling(GlobalStateDev* global_state,
                                                   uint32_t* tot_num_active_units) {
  if (global_state->block_states[blockIdx.x].simulation_completed) {
    return;
  }

  const auto bid = blockIdx.x;
  const auto buff_alignment = global_state->large_uint_buff_chunk_alignment;
  auto* start_offsets = global_state->sorting_offset1_buff;
  auto* end_offsets = global_state->sorting_offset2_buff;

  if (!global_state->block_states[bid].burnin_completed) {
    if (threadIdx.x == 0) {
      start_offsets[bid] = buff_alignment * bid;
      end_offsets[bid] = buff_alignment * bid;
    }
    return;
  }

  auto* buff = global_state->large_uint_buff1 + (bid * buff_alignment);

  const auto num_active_lefs = global_state->block_states[bid].num_active_lefs;
  if (threadIdx.x == 0) {
    memcpy(buff, global_state->block_states[bid].lef_fwd_unit_idx,
           num_active_lefs * sizeof(uint32_t));
    start_offsets[bid] = buff_alignment * bid;
    end_offsets[bid] = (buff_alignment * bid) + num_active_lefs;

    if (tot_num_active_units) {
      atomicAdd(tot_num_active_units, num_active_lefs);
    }
  }
  __syncthreads();

  const auto tid = threadIdx.x;
  auto local_rng_state = global_state->block_states[bid].rng_state[tid];
  constexpr auto scaling_factor = static_cast<float>(static_cast<uint32_t>(-1));
  buff = global_state->large_uint_buff3;

  const auto chunk_size = (num_active_lefs + blockDim.x - 1) / blockDim.x;
  const auto i0 = (bid * buff_alignment) + (threadIdx.x * chunk_size);
  const auto i1 = min(i0 + chunk_size, gridDim.x * buff_alignment);

  // printf("%d:%d - %d; %d; %d; %d\n", bid, tid, chunk_size, i0, i1, buff_alignment * gridDim.x);

  for (auto i = i0; i < i1; ++i) {
    buff[i] = __float2uint_rn(curand_uniform(&local_rng_state) * scaling_factor);
  }

  global_state->block_states[bid].rng_state[tid] = local_rng_state;
}

__global__ void select_lefs_then_register_contacts(GlobalStateDev* global_state) {
  if (global_state->block_states[blockIdx.x].simulation_completed ||
      !global_state->block_states[blockIdx.x].burnin_completed) {
    return;
  }

  const auto tid = threadIdx.x;
  const auto bid = blockIdx.x;

  const auto* rev_unit_idx_buff = global_state->large_uint_buff1;
  const auto* lef_rev_unit_idx_buff = global_state->block_states[bid].lef_rev_unit_idx;

  const auto* rev_pos_buff = global_state->block_states[bid].rev_unit_pos;
  const auto* fwd_pos_buff = global_state->block_states[bid].fwd_unit_pos;

  const auto buff_alignment = global_state->large_uint_buff_chunk_alignment;

  const auto target_sample_size =
      __float2uint_rn(global_state->config->lef_fraction_contact_sampling *
                      static_cast<float>(global_state->block_states[bid].num_active_lefs));
  const auto sample_size =
      min(target_sample_size, global_state->block_states[bid].contact_local_buff_capacity -
                                  global_state->block_states[bid].contact_local_buff_size);
  const auto bin_size = static_cast<float>(global_state->config->bin_size);

  const auto offset = global_state->block_states[bid].contact_local_buff_size;

  const auto chunk_size = (sample_size + blockDim.x - 1) / blockDim.x;
  const auto i0 = chunk_size * tid;
  const auto i1 = min(i0 + chunk_size, sample_size);

  for (auto i = i0; i < i1; ++i) {
    const auto rev_idx = rev_unit_idx_buff[(bid * buff_alignment) + i];
    const auto fwd_idx = lef_rev_unit_idx_buff[rev_idx];

    const auto rev_pos = rev_pos_buff[rev_idx];
    const auto fwd_pos = fwd_pos_buff[fwd_idx];
    // printf("bid=%d; tid=%d; i=%d; %d:%d -> %d:%d\n", bid, tid, i, rev_idx, fwd_idx, rev_pos,
    //        fwd_pos);
    global_state->block_states[bid].contact_local_buff[offset + i].x =
        __float2uint_rn(static_cast<float>(min(rev_pos, fwd_pos)) / bin_size);
    global_state->block_states[bid].contact_local_buff[offset + i].y =
        __float2uint_rn(static_cast<float>(max(rev_pos, fwd_pos)) / bin_size);
  }

  __syncthreads();
  if (threadIdx.x == 0) {
    global_state->block_states[bid].contact_local_buff_size += sample_size;
    if (global_state->block_states[bid].contact_local_buff_size ==
        global_state->block_states[bid].contact_local_buff_capacity) {
      global_state->block_states[blockIdx.x].simulation_completed = true;
      atomicAdd(&global_state->ntasks_completed, 1);
    }
  }
}

}  // namespace modle::cu::kernels
