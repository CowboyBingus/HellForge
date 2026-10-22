# AR-23G Liberator Gold as a CMake project (docs/modding/tools.md): open this folder in Rider, CLion or VS Code, or build it with
#   cmake --preset default && cmake --build --preset default
cmake_minimum_required(VERSION 3.25)
project(golden_liberator C)
include(../../../native/mod-sdk/cmake/HellForgeMod.cmake)

hellforge_add_mod(golden_liberator src/golden_liberator.c)
