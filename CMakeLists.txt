cmake_minimum_required(VERSION 3.19)

project(hyprseam
    DESCRIPTION "Seamless pointer transfer across partially shared monitor edges"
    VERSION 0.1.0
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET hyprland)

file(GLOB_RECURSE SRCFILES CONFIGURE_DEPENDS "src/*.cpp")

add_library(hyprseam SHARED ${SRCFILES})
set_target_properties(hyprseam PROPERTIES PREFIX "")

target_link_libraries(hyprseam PRIVATE PkgConfig::deps)
