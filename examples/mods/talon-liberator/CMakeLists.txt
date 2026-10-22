# AR-23T Liberator Talon as a CMake project (docs/modding/tools.md): open this folder in Rider, CLion or VS Code, or build it with
#   cmake --preset default && cmake --build --preset default
cmake_minimum_required(VERSION 3.25)
project(talon_liberator C)
include(../../../native/mod-sdk/cmake/HellForgeMod.cmake)

hellforge_add_mod(talon_liberator src/talon_liberator.c)
