# Copyright (C) 2022 Roberto Rossini <roberros@uio.no>
#
# SPDX-License-Identifier: MIT

include(FetchContent)

# cmake-format: off
FetchContent_Declare(
        bitflags
        URL ${CMAKE_CURRENT_SOURCE_DIR}/external/bitflags-1.5.0.tar.xz
        URL_HASH SHA512=918b73fd40ce6180c237caae221c0d8bea74b203d75a77ee2c399cbf1e063894f1df4a838d13fd87ca0870551ff83449462f8705e61e1c21c7f3c1e47ba07b71
)
FetchContent_Declare(
        libBigWig
        URL ${CMAKE_CURRENT_SOURCE_DIR}/external/libBigWig-0.4.6.tar.xz
        URL_HASH SHA512=11cfb35da7fa99fe8f73d219654d2cfb838a74b7c6ba5610b826265251bdfcfb4feb2c6e2fc2377edd73154dd971f9604ea0e270bd5830d1f28509f84ad49f7e
)
FetchContent_Declare(
        Xoshiro-cpp
        URL ${CMAKE_CURRENT_SOURCE_DIR}/external/Xoshiro-cpp-1.1.tar.xz
        URL_HASH SHA512=fb584cae675ebdb181801237a1462b0931478cb3123987b06dee8cbb4b6d823fcfa148f38aef184dd3192c985f6fe1984339bb2d5e1399db40501ab81a92ecfb
)
# cmake-format: on

set(WITH_CURL
    OFF
    CACHE INTERNAL "")

FetchContent_MakeAvailable(bitflags libBigWig Xoshiro-cpp)
