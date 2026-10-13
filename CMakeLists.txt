cmake_minimum_required(VERSION 3.16)
# Dependencies are pinned and installed by PlatformIO, not IDF's registry.
set(ENV{IDF_COMPONENT_MANAGER} "0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(room_controller)
# GCC 8 warns about NimBLE's enum-to-error-code macro. Keep the warning visible
# without turning it into an error; all other IDF warning checks remain intact.
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=int-in-bool-context" APPEND)
