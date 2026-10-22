# Machine Gun 10k as a CMake project (docs/modding/tools.md): open this folder in Rider, CLion or VS Code, or build it with
#   cmake --preset default && cmake --build --preset default
cmake_minimum_required(VERSION 3.25)
project(machine_gun_10k C)
include(../../../native/mod-sdk/cmake/HellForgeMod.cmake)

hellforge_add_mod(machine_gun_10k src/machine_gun_10k.c)
