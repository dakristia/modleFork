include(FetchContent)
FetchContent_Declare(
  libBigWig
  URL ${CMAKE_CURRENT_SOURCE_DIR}/external/libBigWig-0.4.6.tar.xz
  URL_HASH
    SHA512=9a7ce0448d79f072040860bc9b9b5514253725be9ecc10f40be4030d91c315a5d81671a7512e03eee993ce05a0b3e59b6a0498773ba4a9b1b1ecd773ff1211e1
)
FetchContent_Declare(
  Xoshiro-cpp
  URL ${CMAKE_CURRENT_SOURCE_DIR}/external/Xoshiro-cpp-1.1.tar.xz
  URL_HASH
    SHA512=b8f7786f56733a284cf678142d0df232dba80d46e427d80bc360cc853c8617d80da3c49bf4654fea6635119fb4b86532a777ca66226262363537bd8625530354
)

FetchContent_GetProperties(libBigWig)
FetchContent_GetProperties(Xoshiro)

if(NOT ${libbigwig}_POPULATED)
  FetchContent_Populate(libBigWig)
endif()
add_subdirectory(${libbigwig_SOURCE_DIR} ${libbigwig_BINARY_DIR})

if(NOT MODLE_USE_MERSENNE_TWISTER)
  if(NOT ${xoshiro-cpp}_POPULATED)
    FetchContent_Populate(Xoshiro-cpp)
  endif()
  add_subdirectory(${xoshiro-cpp_SOURCE_DIR} ${xoshiro-cpp_BINARY_DIR})
else()
  target_compile_definitions(project_options INTERFACE MODLE_USE_MERSENNE_TWISTER=1)
endif()
