# The Stratagem Compendium as a CMake project (docs/modding/tools.md): open this folder in Rider, CLion or VS Code, or build it with
#   cmake --preset default && cmake --build --preset default
cmake_minimum_required(VERSION 3.25)
project(stratagem_compendium C)
include(../../../native/mod-sdk/cmake/HellForgeMod.cmake)

hellforge_add_mod(stratagem_compendium src/compendium.c src/pelicans.c src/hmg_sentry.c src/orbital_barrages.c src/expendables.c
    src/shredder_silo.c)
