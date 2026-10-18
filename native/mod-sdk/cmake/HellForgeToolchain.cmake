# CMake toolchain for HellForge code mods: HellForge's pinned Zig compiles C and C++ for 64-bit Windows, so no Visual
# Studio is needed. Mod presets point here (CMakePresets.json "toolchainFile"). The tools are three levels above this
# folder in both layouts: <repository>/tools (from native/mod-sdk/cmake) and <installation>/tools (from app/sdk/cmake).
get_filename_component(HELLFORGE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(HELLFORGE_ZIG "${HELLFORGE_ROOT}/tools/zig/zig.exe")
if(NOT EXISTS "${HELLFORGE_ZIG}")
    message(FATAL_ERROR "Zig is missing: install the mod tools (the launcher's Mods page, HellForge.Cli mod-tools --install ${HELLFORGE_ROOT} --ide yes, or scripts/Setup-ModTools.ps1 in the repository).")
endif()

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER "${HELLFORGE_ZIG}")
set(CMAKE_C_COMPILER_ARG1 cc)
set(CMAKE_CXX_COMPILER "${HELLFORGE_ZIG}")
set(CMAKE_CXX_COMPILER_ARG1 c++)
set(CMAKE_C_COMPILER_TARGET x86_64-windows-gnu)
set(CMAKE_CXX_COMPILER_TARGET x86_64-windows-gnu)
