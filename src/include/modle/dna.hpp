#pragma once

#include <random>
#include <string_view>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "modle/contacts.hpp"

namespace modle {

class ExtrusionBarrier;
class ExtrusionUnit;

/// Class used to represent a DNA molecule as a vector Bin%s.
/** Aside from owning the \p std::vector<Bin>, DNA is also used to keep track of the length and
 * bin size of the DNA molecule.
 */
class DNA {
 public:
  /** Enumerator used across ModLE's code-base to describe directionality in terms of DNA strand
   * (+/- or forward/reverse).
   */
  enum Direction { none = 0, fwd = 1, rev = 2, both = 3 };

  /// Class to represent a single Bin from a DNA molecule
  /** A DNA::Bin knows of the genomic coordinates corresponding to its DNA::Bin::_start and
   * DNA::Bin::_end positions, as well as the entities that are currently bound to it (currently
   * only ExtrusionBarrier and ExtrusionUnit).
   */
  class Bin {
    friend class DNA;

   public:
    ///@{
    /** DNA::Bin Constructor
     * @param idx        Index in the \p std::vector that owns this instance of DNA::Bin.
     * @param start      Genomic coordinate of the left-most position represented by this DNA::Bin.
     * @param end        Genomic coordinate of the right-most position represented by this DNA::Bin.
     * @param barriers   List of ExtrusionBarrier%s mapped in the genomic region represented by this
     * DNA::Bin.
     * \b IMPORTANT: This constructor takes ownership of \p barriers by moving it to the
     * DNA::Bin instance that is being constructed.
     */
    Bin(uint32_t idx, uint64_t start, uint64_t end, std::vector<ExtrusionBarrier>& barriers);
    Bin(uint32_t idx, uint64_t start, uint64_t end);
    ///@}

    [[nodiscard]] uint32_t get_start() const;
    [[nodiscard]] uint32_t get_end() const;
    [[nodiscard]] uint32_t size() const;
    [[nodiscard]] uint32_t get_n_extr_units() const;
    [[nodiscard]] uint32_t get_index() const;
    /** Given a ptr to an instance of ExtrusionBarrier that belongs to this instance of
     * DNA::Bin, return a ptr to the next instance of ExtrusionBarrier that is blocking
     * ExtrusionUnit%s that are moving in the direction specified by \p d.
     * @param b Pointer to ExtrusionBarrier to use to look for the next ExtrusionBarrier (i.e.
     * DNA::Bin::get_next_extr_barrier will only search for ExtrusionBarriers that come after \p b).
     * When <tt> b == nullptr </tt>, then the search will involve all the ExtrusionBarrier that are
     * bound to the current instance of DNA::Bin.
     * @param d DNA::Direction of the ExtrusionBarrier to look for (the caller has to make sure that
     * <tt> d != DNA::Direction::none </tt>).
     * @return Pointer to the next ExtrusionBarrier with the orientation specified by \p d (or
     * \p nullptr if there are no ExtrusionBarrier%s matching the search parameters).
     */
    [[nodiscard]] ExtrusionBarrier* get_next_extr_barrier(ExtrusionBarrier* b,
                                                          Direction d = DNA::Direction::both) const;
    [[nodiscard]] std::vector<ExtrusionBarrier>* get_all_extr_barriers() const;
    [[nodiscard]] absl::InlinedVector<ExtrusionUnit*, 10>& get_extr_units();

    void add_extr_barrier(ExtrusionBarrier b);
    void add_extr_barrier(double prob_of_barrier_block, DNA::Direction direction);
    /** Register the ExtrusionUnit unit as being bound to this instance of DNA::Bin.
     *
     * This function will take care of allocating the \p std::vector<ExtrusionUnit> when
     * <tt> DNA::Bin::_extr_barriers = nullptr </tt>.
     * @param unit Pointer to the ExtrusionUnit to be registered as binding to this DNA::Bin.
     */
    uint32_t add_extr_unit_binding(ExtrusionUnit* unit);
    /** Remove the ExtrusionUnit \p unit from the list of ExtrusionUnit%s that are bound to this
     * instance of DNA::Bin.
     *
     * This function will take care of deallocating the \p std::vector<ExtrusionUnit> when there are
     * no more ExtrusionUnit%s bound to this DNA::Bin.
     * @param unit Pointer to the unit to be removed from the \p std::vector<ExtrusionUnit> bound to
     * this DNA::Bin.
     */
    uint32_t remove_extr_unit_binding(ExtrusionUnit* unit);
    void remove_extr_barrier(Direction d);
    uint32_t remove_all_extr_barriers();

   private:
    /// Index corresponding to this DNA::Bin in the owning \p std::vector<DNA::Bin> (DNA::_bins).
    uint32_t _idx;
    uint32_t _start;  ///< Left-most position represented by this DNA::Bin.
    uint32_t _end;    ///< Right-most position represented by this DNA::Bin.
    // TODO: Consider remove the unique_ptr and store the vector and InlinedVector directly
    // This will increase memory footprint, but should improve performance. Needs testing
    /// \brief Pointer to a \p std::vector of ExtrusionBarrier%s (or \p nullptr if there are no
    /// ExtrusionBarrier%s).
    std::unique_ptr<std::vector<ExtrusionBarrier>> _extr_barriers{nullptr};
    /// Ptr to a \p std::vector of ExtrusionUnit%s (or \p nullptr if there are no ExtrusionUnit%s).
    std::unique_ptr<absl::InlinedVector<ExtrusionUnit*, 10>> _extr_units{nullptr};
  };

  DNA(uint64_t length, uint32_t bin_size);

  // Getters
  [[nodiscard]] uint32_t length() const;
  [[nodiscard]] uint32_t get_n_bins() const;
  [[nodiscard]] uint32_t get_n_barriers() const;
  [[nodiscard]] uint32_t get_bin_size() const;
  [[nodiscard]] DNA::Bin& get_bin_from_pos(uint32_t pos);
  [[nodiscard]] DNA::Bin* get_ptr_to_bin_from_pos(uint32_t pos);
  [[nodiscard]] DNA::Bin& get_prev_bin(const Bin& current_bin);
  [[nodiscard]] DNA::Bin* get_ptr_to_prev_bin(const Bin& current_bin);
  [[nodiscard]] DNA::Bin& get_next_bin(const Bin& current_bin);
  [[nodiscard]] DNA::Bin* get_ptr_to_next_bin(const Bin& current_bin);
  [[nodiscard]] DNA::Bin& get_first_bin();
  [[nodiscard]] DNA::Bin* get_ptr_to_first_bin();
  [[nodiscard]] DNA::Bin& get_last_bin();
  [[nodiscard]] DNA::Bin* get_ptr_to_last_bin();

  // Iterator stuff
  [[nodiscard]] std::vector<Bin>::iterator begin();
  [[nodiscard]] std::vector<Bin>::iterator end();
  [[nodiscard]] std::vector<Bin>::const_iterator cbegin() const;
  [[nodiscard]] std::vector<Bin>::const_iterator cend() const;

  // Modifiers
  void add_extr_barrier(ExtrusionBarrier& b, uint32_t pos);
  void remove_extr_barrier(uint32_t pos, Direction direction);

 private:
  std::vector<Bin> _bins;  ///< \p Bin%s belonging to this instance of DNA.
  uint64_t _length;        ///< DNA molecule length in bp.
  uint32_t _bin_size;      ///< Target bin size in bp.

  /** Initialize the <tt> std::vector<Bin> </tt> given a length and bin size.
   *
   * When <tt> length > bin_size </tt> the <tt> std::vector<Bin> </tt> will contain a single
   * DNA::Bin starting at <tt> pos == 0 </tt> and ending at <tt> pos == length </tt>.
   *
   * When <tt> length % bin_size != 0 </tt> then the last bin in <tt> std::vector<DNA::Bin> </tt>
   * will be shorter than  <tt> bin_size </tt>.
   *
   * This will introduce a bias towards the last DNA::Bin when randomly selecting DNA::Bin by index.
   * This bias should be negligible for <tt> length >> bin_size </tt> (which should be the case
   * under normal circumstances).
   * @param length    Length of DNA molecule (bp).
   * @param bin_size  Bin size (bp).
   * @return          A vector of DNA::Bin with where consecutive DNA::Bin are non-overlapping (i.e.
   * given a pair of consecutive DNA::Bin%s <tt> b1 </tt> and <tt> b2 </tt>, <tt> b1::_end ==
   * b2::_start + 1 </tt>.
   */
  static std::vector<Bin> make_bins(uint64_t length, uint32_t bin_size);
};

/// Struct representing a Chromosome.
/** The purpose of this struct is to keep together data regarding a single DNA molecule
 */
struct Chromosome {
  Chromosome(std::string name, uint64_t length, uint32_t bin_size, uint32_t avg_lef_processivity);

  // Getters
  [[nodiscard]] uint32_t length() const;
  [[nodiscard]] uint32_t get_n_bins() const;
  [[nodiscard]] uint32_t get_n_barriers() const;

  void write_contacts_to_tsv(std::string_view chr_name, std::string_view output_dir,
                             bool write_full_matrix = false) const;

  std::string name;  ///< Sequence name (e.g. chromosome name from a BED file).
  DNA dna;           ///< DNA molecule represented as a sequence of DNA::Bin%s.
  /// Pointers to the ExtrusionBarrier associated to \p Chromosome::dna.
  std::vector<ExtrusionBarrier*> barriers;
  ContactMatrix<uint32_t> contacts;  ///< ContactMatrix for the DNA::Bin%s from Chromosome::dna.
};

};  // namespace modle
