cmake_minimum_required(VERSION 3.14)
project(tangerine)

if (MSVC)
	message(FATAL_ERROR 
		"Currently CMake is only used on the Linux platform.\
		Visual Studio users should open the .sln file directly, instead.")
endif()

option(EMBED_LUA "Embed Lua support" ON)
option(EMBED_RACKET "Embed Racket support" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 17)

file(GLOB_RECURSE TANGERINE_FILES "tangerine/*.cpp")

if(EMBED_LUA)
	file(GLOB TANGERINE_LUA_FILES "third_party/lua-5.4.4/lua/*.c")
	list(FILTER
		TANGERINE_LUA_FILES
		EXCLUDE REGEX
		".*/lua/(onelua|lua|luac)\.c") # these define main
endif()

file(GLOB TANGERINE_IMGUI_FILES
	"third_party/imgui/*.cpp")

set(TANGERINE_THIRD_PARTY_FILES
	${TANGERINE_IMGUI_FILES}
	"third_party/imgui/backends/imgui_impl_opengl3.cpp"
	"third_party/imgui/backends/imgui_impl_sdl.cpp"
	"third_party/ImFileDialog/ImFileDialog.cpp"
	"third_party/stb/stb.cpp"
	"third_party/voxwriter/VoxWriter.cpp"
	"third_party/fmt/src/format.cc"
	"third_party/glad/glad.c"
	${TANGERINE_LUA_FILES})

set(TANGERINE_INCLUDE_DIRS
	"third_party"
	"third_party/fmt/include"
	"third_party/imgui"
	"third_party/imgui/backends"
	"third_party/ImFileDialog"
	"third_party/stb")

if(EMBED_LUA)
	list(APPEND TANGERINE_INCLUDE_DIRS "third_party/lua-5.4.4")
endif()
if(EMBED_RACKET)
	list(APPEND TANGERINE_INCLUDE_DIRS "third_party/racket/include")
endif()

add_executable(tangerine
	${TANGERINE_FILES}
	${TANGERINE_THIRD_PARTY_FILES})

find_package(SDL2 REQUIRED)

find_package(PkgConfig REQUIRED)

include_directories(
	${TANGERINE_INCLUDE_DIRS})

target_compile_definitions(tangerine PRIVATE
	"EMBED_LUA=$<BOOL:${EMBED_LUA}>"
	"EMBED_RACKET=$<BOOL:${EMBED_RACKET}>")

target_link_libraries(tangerine
	SDL2::SDL2
	"$<$<BOOL:${EMBED_RACKET}>:racketcs>"
	tinfo)
