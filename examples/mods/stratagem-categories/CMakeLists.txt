cmake_minimum_required(VERSION 3.25)
project(stratagem_categories C)
include(../../../native/mod-sdk/cmake/HellForgeMod.cmake)
hellforge_add_mod(stratagem_categories src/categories.c)
