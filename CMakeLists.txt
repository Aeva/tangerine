cmake_minimum_required(VERSION 3.14)
project(tangerine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 17)

file(GLOB_RECURSE TANGERINE_FILES "tangerine/*.cpp")

file(GLOB TANGERINE_LUA_FILES "third_party/lua-5.4.4/lua/*.c")
list(FILTER
    TANGERINE_LUA_FILES
    EXCLUDE REGEX
    ".*/lua/(onelua|lua|luac)\.c") # these define main

file(GLOB TANGERINE_IMGUI_FILES
	"third_party/imgui/*.cpp")

set(TANGERINE_THIRD_PARTY_FILES
	${TANGERINE_IMGUI_FILES}
	"third_party/imgui/backends/imgui_impl_opengl3.cpp"
	"third_party/imgui/backends/imgui_impl_sdl.cpp"
	"third_party/voxwriter/VoxWriter.cpp"
	"third_party/fmt/src/format.cc"
	"third_party/glad/glad.c"
	${TANGERINE_LUA_FILES})

set(TANGERINE_INCLUDE_DIRS
	"third_party"
	"third_party/fmt/include"
	"third_party/imgui"
	"third_party/imgui/backends"
	"third_party/racket/include"
	"third_party/lua-5.4.4")

add_executable(tangerine
	${TANGERINE_FILES}
	${TANGERINE_THIRD_PARTY_FILES})

find_package(SDL2 REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK2 REQUIRED gtk+-2.0)
include_directories(
	${TANGERINE_INCLUDE_DIRS}
	${GTK2_INCLUDE_DIRS})

target_link_libraries(tangerine
	SDL2::SDL2
	racketcs
	tinfo
	${GTK2_LIBRARIES})
