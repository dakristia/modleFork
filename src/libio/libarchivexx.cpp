#include "modle/libarchivexx.hpp"

#include <archive.h>        // for archive
#include <archive_entry.h>  // for archive_entry
#include <fmt/format.h>     // system_error
#include <fmt/ostream.h>

#include <cassert>     // for assert
#include <filesystem>  // for path
#include <memory>      // for unique_ptr
#include <string>      // for string

namespace modle::libarchivexx {
Reader::Reader(const std::filesystem::path& path, size_t buff_capacity) {
  this->_buff.reserve(buff_capacity);
  this->open(path);
}

void Reader::open(const std::filesystem::path& path) {
  auto handle_open_errors = [&](la_ssize_t status) {
    if (status != ARCHIVE_OK) {
      throw fmt::system_error(archive_errno(this->_arc.get()),
                              FMT_STRING("Failed to open file {} for reading"), this->_path);
    }
  };

  if (this->is_open()) {
    this->close();
  }

  this->_path = path;
  this->_arc.reset(archive_read_new());
  if (!this->_arc) {
    throw std::runtime_error(
        fmt::format(FMT_STRING("Failed to allocate a buffer of to read file {}"), this->_path));
  }

  handle_open_errors(archive_read_support_filter_all(this->_arc.get()));
  handle_open_errors(archive_read_support_format_raw(this->_arc.get()));
  handle_open_errors(
      archive_read_open_filename(this->_arc.get(), this->_path.c_str(), this->_buff.capacity()));
  handle_open_errors(archive_read_next_header(this->_arc.get(), this->_arc_entry.get()));
}

bool Reader::eof() const noexcept {
  assert(this->is_open());  // NOLINT
  return this->_eof;
}

bool Reader::is_open() const noexcept { return !!this->_arc; }

void Reader::close() {
  this->_arc = nullptr;
  this->_buff.clear();
  this->_eof = false;
}

void Reader::reset() {
  this->close();
  this->open(this->_path);
}

const std::filesystem::path& Reader::path() const noexcept { return this->_path; }
std::string Reader::path_string() const noexcept { return this->_path.string(); }
const char* Reader::path_c_str() const noexcept { return this->_path.c_str(); }

void Reader::handle_libarchive_errors(la_ssize_t errcode) const {
  if (errcode != ARCHIVE_OK) {
    this->handle_libarchive_errors();
  }
}

void Reader::handle_libarchive_errors() const {
  if (const auto status = archive_errno(this->_arc.get()); status != 0) {
    throw fmt::system_error(archive_errno(this->_arc.get()),
                            FMT_STRING("The following error occurred while reading file {}"),
                            this->_path);
  }
}

bool Reader::getline(std::string& buff, char sep) {
  assert(this->is_open());  // NOLINT
  buff.clear();
  if (this->eof()) {
    return false;
  }

  while (!this->read_next_token(buff, sep)) {
    if (!this->read_next_chunk()) {
      assert(this->eof());  // NOLINT
      return false;
    }
  }
  return true;
}

bool Reader::read_next_chunk() {
  assert(!this->eof());     // NOLINT
  assert(this->is_open());  // NOLINT
  this->_buff.resize(this->_buff.capacity());
  const auto bytes_read =
      archive_read_data(this->_arc.get(), this->_buff.data(), this->_buff.capacity());
  if (bytes_read < 0) {
    handle_libarchive_errors();
  } else if (bytes_read == 0) {
    this->_eof = true;
    this->_buff.clear();
    return false;
  }
  this->_buff.resize(static_cast<size_t>(bytes_read));
  this->_idx = 0;
  return true;
}

bool Reader::read_next_token(std::string& buff, char sep) {
  assert(!this->eof());                      // NOLINT
  assert(this->is_open());                   // NOLINT
  assert(this->_idx <= this->_buff.size());  // NOLINT
  if (this->_idx == this->_buff.size()) {
    return false;
  }

  const auto pos = this->_buff.find(sep, this->_idx);
  const auto i = static_cast<int64_t>(this->_idx);
  if (pos == std::string::npos) {
    buff.append(this->_buff.begin() + i, this->_buff.end());
    return false;
  }

  assert(pos >= this->_idx);  // NOLINT
  buff.append(this->_buff.begin() + i, this->_buff.begin() + static_cast<int64_t>(pos));
  this->_idx = pos + 1;
  return true;
}

}  // namespace modle::libarchivexx
