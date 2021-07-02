#pragma once
#include <absl/container/flat_hash_set.h>  // for flat_hash_set, operator!=
#include <absl/hash/hash.h>                // for Hash
#include <absl/strings/str_cat.h>          // for StrCat

#include <algorithm>                  // for max, generate
#include <array>                      // for array
#include <boost/filesystem/path.hpp>  // for create_directories, exists, path, remove
#include <cstdint>                    // for uint32_t, int32_t, int64_t
#include <cstdio>                     // for pclose, fgets, popen, FILE
#include <fstream>                    // for basic_ofstream, operator<<, basic_ostream
#include <memory>                     // for allocator, unique_ptr
#include <numeric>                    // for iota
#include <stdexcept>                  // for runtime_error
#include <string>                     // for string, operator+, char_traits, stod
#include <type_traits>                // for enable_if, is_arithmetic
#include <utility>                    // for pair
#include <vector>                     // for vector

namespace modle::test::correlation {

template <typename N, typename = typename std::enable_if<std::is_arithmetic<N>::value, N>::type>
inline void write_vect_to_file(const boost::filesystem::path& fpath, const std::vector<N>& v) {
  auto fp = std::ofstream(fpath.string());
  fp << v[0];
  for (auto i = 1UL; i < v.size(); ++i) {
    fp << "," << v[i];
  }
  fp << std::endl;
  fp.close();
}

inline std::vector<uint32_t> generate_random_vect(std::mt19937& rnd_eng, uint32_t size,
                                                  uint32_t min, uint32_t max,
                                                  bool allow_duplicates = true) {
  random::uniform_int_distribution<uint32_t> dist(min, max);
  std::vector<uint32_t> v(size);
  if (allow_duplicates) {
    std::generate(v.begin(), v.end(), [&]() { return dist(rnd_eng); });
  } else {
    absl::flat_hash_set<uint32_t> s;
    while (s.size() < size) {
      s.insert(dist(rnd_eng));
    }
    v = {s.begin(), s.end()};
  }
  return v;
}

inline std::pair<std::vector<uint32_t>, std::vector<uint32_t>> generate_correlated_vects(
    std::mt19937& rnd_eng, uint32_t size) {
  random::uniform_int_distribution<int32_t> dist(static_cast<int32_t>(size) / -50,  // NOLINT
                                                 static_cast<int32_t>(size / 50));  // NOLINT
  std::vector<uint32_t> v1(size);
  std::vector<uint32_t> v2(size);
  std::iota(v1.begin(), v1.end(), 0);
  std::iota(v2.begin(), v2.end(), 0);
  for (auto i = 0UL; i < size; ++i) {
    DISABLE_WARNING_PUSH
    DISABLE_WARNING_USELESS_CAST
    int64_t n = static_cast<int64_t>(v1[i]) + dist(rnd_eng);
    v1[i] = static_cast<uint32_t>(std::max(int64_t(0), n));
    n = static_cast<int64_t>(v2[i]) + dist(rnd_eng);
    v2[i] = static_cast<uint32_t>(std::max(int64_t(0), n));
    DISABLE_WARNING_POP
  }
  return {v1, v2};
}

template <typename N, typename = typename std::enable_if<std::is_arithmetic<N>::value, N>::type>
[[nodiscard]] inline std::pair<double, double> corr_scipy(const std::vector<N>& v1,
                                                          const std::vector<N>& v2,
                                                          const std::string& method,
                                                          const boost::filesystem::path& tmpdir) {
  if (!boost::filesystem::exists(tmpdir)) {
    boost::filesystem::create_directories(tmpdir);
  }
  const auto f1_path = tmpdir / absl::StrCat(boost::hash_range(v1.begin(), v1.end()), "_f1");
  const auto f2_path = tmpdir / absl::StrCat(boost::hash_range(v2.begin(), v2.end()), "_f2");
  write_vect_to_file(f1_path, v1);
  write_vect_to_file(f2_path, v2);

  const std::string cmd =
      "python3 -c '"
      "from scipy.stats import pearsonr, spearmanr, kendalltau; from sys import argv, stderr; "
      "from numpy import genfromtxt; "
      "v1 = genfromtxt(argv[1], delimiter=\",\", dtype=int); "
      "v2 = genfromtxt(argv[2], delimiter=\",\", dtype=int); "
      "corr, pv = " +
      method +
      "(v1, v2); "
      //      "print(v1, file=stderr); print(v2, file=stderr); "
      "print(f\"{corr:.16e}\\t{pv:.16e}\", end=\"\");' " +
      f1_path.string() + " " + f2_path.string();
  // NOLINTNEXTLINE(readability-magic-numbers, cppcoreguidelines-avoid-magic-numbers)
  std::array<char, 256> buffer{};
  std::string result;
  // TODO: replace this with boost::process
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
    //    printf("%s\n", result.c_str());
  }
  boost::filesystem::remove(f1_path);
  boost::filesystem::remove(f2_path);

  const auto rho = std::stod(std::string(result.data(), result.find('\t')));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  const auto pv = std::stod(std::string(result.data() + result.find('\t')));

  return {rho, pv};
}
}  // namespace modle::test::correlation
