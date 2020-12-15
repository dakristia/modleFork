#pragma once

#include <H5Cpp.h>

#include <string_view>
#include <vector>
#include <cstdint>


#include "modle/contacts.hpp"

namespace modle::cooler {
template <typename DataType>
[[nodiscard]] H5::PredType getH5_type();

[[nodiscard]] H5::H5File init_file(std::string_view path_to_output_file,
                                   bool force_overwrite = false);
template <typename S>
hsize_t write_vect_of_str(std::vector<S> &data, H5::H5File &f, std::string_view dataset_name,
                          hsize_t file_offset = 0, hsize_t CHUNK_DIMS = 512 * 1024UL, /* 512 KB */
                          uint8_t COMPRESSION_LEVEL = 6);

hsize_t write_vect_of_enums(std::vector<int32_t> &data, const H5::EnumType &ENUM, H5::H5File &f,
                            std::string_view dataset_name, hsize_t file_offset = 0,
                            hsize_t CHUNK_DIMS = 1024 * 1024UL / sizeof(int32_t), /* 1 MB */
                            uint8_t COMPRESSION_LEVEL = 6);

template <typename I>
hsize_t write_vect_of_int(std::vector<I> &data, H5::H5File &f, std::string_view dataset_name,
                          hsize_t file_offset = 0,
                          hsize_t CHUNK_DIMS = 1024 * 1024UL / sizeof(I), /* 1 MB */
                          uint8_t COMPRESSION_LEVEL = 6);

void write_chroms(H5::H5File &f,
                  const typename std::vector<ContactMatrix<int64_t>::Header> &headers);

void write_contacts(H5::H5File &f, const std::vector<std::string_view> &path_to_cmatrices);

void write_metadata(H5::H5File &f, int32_t bin_size, std::string_view assembly_name = "");

void modle_to_cooler(const std::vector<std::string_view> &path_to_cmatrices,
                     std::string_view path_to_output);

H5::EnumType init_enum_from_strs(const std::vector<std::string> &data, int32_t offset);
}  // namespace modle::cooler

#include "../../cooler_impl.hpp"
