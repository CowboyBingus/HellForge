# {{name}} as a CMake project: open this folder in Rider, CLion or VS Code, or build it with
#   cmake --preset default && cmake --build --preset default
# (or, without CMake: HellForge.Cli build-mod --mod <this folder>, or Build now in the mod's menu on the Mods page).
cmake_minimum_required(VERSION 3.25)
project({{dll}} C)
include({{sdk}}/cmake/HellForgeMod.cmake)

hellforge_add_mod({{dll}} src/{{dll}}.c)
