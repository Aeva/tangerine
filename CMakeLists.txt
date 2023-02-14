cmake_minimum_required(VERSION 3.24)
project(tangerine
	DESCRIPTION "Procedural 3D model creation")

# Add find modules to the path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/linux/cmake")

if (MSVC)
	message(FATAL_ERROR 
		"Currently CMake is only used on the Linux platform.\
		Visual Studio users should open the .sln file directly, instead.")
endif()

include(GNUInstallDirs)

find_package(Racket REQUIRED) #FIXME for testing

#######################################################################
## Build options:

option(EMBED_LUA "Embed Lua support" ON)
option(EMBED_RACKET "Embed Racket support" OFF)
option(SELF_CONTAINED "Use directory tree instead of XDG_STATE_HOME" OFF)

set(INSTALL_PKG_SUBPATH "tangerine" CACHE PATH
	"Subdirectory to form PKGDATADIR from DATADIR")
mark_as_advanced(INSTALL_PKG_SUBPATH)

#######################################################################
## Required dependencies:

find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 17)

#######################################################################
##+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++##
#######################################################################
## Set up PKGDATADIR (e.g. "share/tangerine/shaders"):

cmake_path(APPEND CMAKE_INSTALL_DATADIR "${INSTALL_PKG_SUBPATH}"
	OUTPUT_VARIABLE INSTALL_PKGDATADIR)
cmake_path(RELATIVE_PATH INSTALL_PKGDATADIR
	BASE_DIRECTORY CMAKE_INSTALL_BINDIR
	OUTPUT_VARIABLE PKGDATADIR_FROM_BINDIR)
cmake_path(APPEND CMAKE_BINARY_DIR "${INSTALL_PKGDATADIR}"
	OUTPUT_VARIABLE BUILD_PKGDATADIR)
file(GLOB_RECURSE SHADER_FILES CONFIGURE_DEPENDS
	"shaders/*.glsl"
	"shaders/defines.h")
if(EMBED_LUA)
	file(GLOB_RECURSE MODELS_LUA_FILES CONFIGURE_DEPENDS "models/*.lua")
endif()
if(EMBED_RACKET)
	file(GLOB_RECURSE MODELS_RKT_FILES CONFIGURE_DEPENDS "models/*.rkt")
endif()
cmake_path(APPEND BUILD_PKGDATADIR "shaders" OUTPUT_VARIABLE BUILD_SHADERS_DIR)
file(MAKE_DIRECTORY "${BUILD_SHADERS_DIR}")
foreach(X IN LISTS SHADER_FILES)
	configure_file("${X}" "${BUILD_SHADERS_DIR}" COPYONLY)
endforeach()
cmake_path(APPEND BUILD_PKGDATADIR "models" OUTPUT_VARIABLE BUILD_MODELS_DIR)
file(MAKE_DIRECTORY "${BUILD_MODELS_DIR}")
foreach(X IN LISTS MODELS_LUA_FILES MODELS_RKT_FILES)
	configure_file("${X}" "${BUILD_MODELS_DIR}" COPYONLY)
endforeach()
install(DIRECTORY "${BUILD_PKGDATADIR}/"
	DESTINATION $<PATH:APPEND,${CMAKE_INSTALL_FULL_DATADIR},${INSTALL_PKG_SUBPATH}>)


#######################################################################
##+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++##
#######################################################################
## Third-Party Libraries:

#######################################################################
## fmt:

add_library(fmt OBJECT "third_party/fmt/src/format.cc")
target_include_directories(fmt PUBLIC "third_party/fmt/include")

#######################################################################
## GLAD:

add_library(glad OBJECT "third_party/glad/glad.c")

#######################################################################
## GLM:

add_subdirectory("third_party/glm")

#######################################################################
## ImGui:

file(GLOB TANGERINE_IMGUI_FILES CONFIGURE_DEPENDS
	"third_party/imgui/*.cpp"
	"third_party/imgui/backends/imgui_impl_opengl3.cpp"
	"third_party/imgui/backends/imgui_impl_sdl.cpp")
add_library(imgui OBJECT "${TANGERINE_IMGUI_FILES}")
target_link_libraries(imgui PUBLIC SDL2::SDL2)
# some complexity to make both <imgui/imgui.h> and "imgui.h" work
# FIXME same problem with glm ... can we just move the source?
target_include_directories(imgui PUBLIC
	$<BUILD_INTERFACE:$<PATH:APPEND,${CMAKE_BINARY_DIR},imgui-include-dir>>
	$<BUILD_INTERFACE:$<PATH:APPEND,${CMAKE_BINARY_DIR},imgui-include-dir/imgui>>
	$<BUILD_INTERFACE:$<PATH:APPEND,${CMAKE_BINARY_DIR},imgui-include-dir/imgui/backends>>
	$<INSTALL_INTERFACE:"${CMAKE_INSTALL_FULL_INCLUDEDIR}">)
file(GLOB TANGERINE_IMGUI_HEADERS CONFIGURE_DEPENDS "third_party/imgui/*.h")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/imgui-include-dir/imgui/backends")
foreach(X IN ITEMS "third_party/imgui/backends/imgui_impl_opengl3.h"
			"third_party/imgui/backends/imgui_impl_sdl.h")
	configure_file("${X}" "imgui-include-dir/imgui/backends" COPYONLY)
endforeach()
foreach(X IN LISTS TANGERINE_IMGUI_HEADERS)
	configure_file("${X}" "imgui-include-dir/imgui")
endforeach()

#######################################################################
## ImFileDialog:

add_library(ImFileDialog OBJECT
	"third_party/ImFileDialog/ImFileDialog.cpp"
	"third_party/stb/stb.cpp")
target_link_libraries(ImFileDialog PUBLIC imgui)
target_include_directories(ImFileDialog
	PUBLIC "third_party/ImFileDialog" #/ImFileDialog.h"
	PRIVATE "third_party/stb") #/stb_image.h")

#######################################################################
## VoxWriter:

add_library(VoxWriter OBJECT "third_party/voxwriter/VoxWriter.cpp")
target_sources(VoxWriter
	PUBLIC FILE_SET HEADERS
	FILES "third_party/voxwriter/VoxWriter.h")


#######################################################################
##+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++##
#######################################################################
## Tangerine, the executable:

file(GLOB_RECURSE TANGERINE_FILES CONFIGURE_DEPENDS "tangerine/*.cpp")
add_executable(tangerine ${TANGERINE_FILES})
set_target_properties(tangerine
	PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY
	$<PATH:APPEND,${CMAKE_BINARY_DIR},${CMAKE_INSTALL_BINDIR}>)
target_compile_definitions(tangerine PRIVATE
	$<$<BOOL:${SELF_CONTAINED}>:"TANGERINE_SELF_CONTAINED">
	"TANGERINE_PKGDATADIR_FROM_BINDIR=${PKGDATADIR_FROM_BINDIR}")
target_link_libraries(tangerine PRIVATE
	fmt
	glad
	glm
	imgui
	ImFileDialog
	VoxWriter
	tinfo # see https://gitlab.kitware.com/cmake/cmake/-/issues/23236
	${CMAKE_THREAD_LIBS_INIT}
	${CMAKE_DL_LIBS})
install(TARGETS tangerine)


#######################################################################
##+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++##
#######################################################################
## Optional Scripting Languages:

#######################################################################
## Lua:

if(EMBED_LUA)
	file(GLOB TANGERINE_LUA_FILES CONFIGURE_DEPENDS "third_party/lua-5.4.4/lua/*.c")
	list(FILTER
		TANGERINE_LUA_FILES
		EXCLUDE REGEX
		".*/lua/(onelua|lua|luac)\.c") # these define main
	add_library(lua OBJECT "${TANGERINE_LUA_FILES}")
	target_include_directories(lua INTERFACE "third_party/lua-5.4.4")
	target_link_libraries(tangerine PRIVATE lua)
endif()
target_compile_definitions(tangerine PRIVATE
	"EMBED_LUA=$<BOOL:${EMBED_LUA}>")

#######################################################################
## Racket:

# FIXME!
