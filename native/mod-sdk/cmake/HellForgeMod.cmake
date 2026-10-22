# hellforge_add_mod(<dll name> <sources>...): builds a HellForge code mod (docs/modding/tools.md) against the mod SDK, into
# <dll name>.dll next to the mod's hellforge-mod.json, which must name it ("code": {"dll": "<dll name>.dll"}).
# With HELLFORGE_INSTALL set to an installation (CMakePresets.json sets <Documents>/HellForge), every build also copies
# the mod - manifest, DLL and data/ - into <installation>/mods/<mod folder name>, so the next session loads it.
include_guard(GLOBAL)
get_filename_component(HELLFORGE_SDK "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(HELLFORGE_INSTALL "" CACHE PATH "A HellForge installation to copy the mod into after each build (empty: none)")

function(hellforge_add_mod name)
    add_library(${name} SHARED ${ARGN})
    target_include_directories(${name} PRIVATE "${HELLFORGE_SDK}")
    set_target_properties(${name} PROPERTIES
        PREFIX "" SUFFIX ".dll"
        C_STANDARD 11 C_STANDARD_REQUIRED ON CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # The same flags as scripts/Build-Mod.ps1.
    target_compile_options(${name} PRIVATE -Wall -Werror -Wno-unused-parameter)
    target_link_options(${name} PRIVATE $<$<CONFIG:Release>:-s>)

    set(manifest "${CMAKE_CURRENT_SOURCE_DIR}/hellforge-mod.json")
    if(NOT EXISTS "${manifest}")
        message(FATAL_ERROR "No hellforge-mod.json in ${CMAKE_CURRENT_SOURCE_DIR}.")
    endif()
    file(READ "${manifest}" text)
    string(JSON dll ERROR_VARIABLE missing GET "${text}" code dll)
    if(missing OR NOT dll STREQUAL "${name}.dll")
        message(FATAL_ERROR "hellforge-mod.json must name the DLL: \"code\": {\"dll\": \"${name}.dll\", \"sdk\": 2}.")
    endif()

    if(HELLFORGE_INSTALL)
        if(NOT EXISTS "${HELLFORGE_INSTALL}/hellforge-install.json" AND NOT EXISTS "${HELLFORGE_INSTALL}/bts-install.json")
            message(WARNING "HELLFORGE_INSTALL (${HELLFORGE_INSTALL}) is not a HellForge installation; the mod is not copied there.")
            return()
        endif()
        get_filename_component(folder "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
        set(target "${HELLFORGE_INSTALL}/mods/${folder}")
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${target}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${manifest}" "$<TARGET_FILE:${name}>" "${target}"
            COMMENT "Copying the mod into ${target}")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data" "${target}/data")
        endif()
    endif()
endfunction()
