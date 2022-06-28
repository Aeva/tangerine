#!/bin/bash
cd `dirname $0`
cd ..

LUA_DIR=third_party/lua-5.4.4/lua

clang++ \
	-std=c++17 \
	-fPIC \
	-U_WIN64 \
	-Ithird_party/fmt/include \
	-Ithird_party/imgui \
	-Ithird_party/imgui/backends \
	-Ithird_party/racket/include \
	-Ithird_party/lua-5.4.4 \
	-Ithird_party \
	-I/usr/include/racket \
	-L/usr/lib/racket \
	`pkg-config --libs --cflags sdl2 gtk+-3.0` \
	tangerine/*.cpp \
	third_party/imgui/*.cpp \
	third_party/imgui/backends/imgui_impl_opengl3.cpp \
	third_party/imgui/backends/imgui_impl_sdl.cpp \
	third_party/voxwriter/VoxWriter.cpp \
	third_party/fmt/src/format.cc \
	-xc \
	-std=c17 \
	third_party/glad/glad.c \
	$LUA_DIR/lapi.c \
	$LUA_DIR/ldebug.c \
	$LUA_DIR/llex.c \
	$LUA_DIR/lparser.c \
	$LUA_DIR/lauxlib.c \
	$LUA_DIR/ldo.c \
	$LUA_DIR/lmathlib.c \
	$LUA_DIR/lstate.c \
	$LUA_DIR/lbaselib.c \
	$LUA_DIR/ldump.c \
	$LUA_DIR/lmem.c \
	$LUA_DIR/lstring.c \
	$LUA_DIR/lundump.c
	$LUA_DIR/lcode.c \
	$LUA_DIR/lfunc.c \
	$LUA_DIR/loadlib.c \
	$LUA_DIR/lstrlib.c \
	$LUA_DIR/lutf8lib.c
	$LUA_DIR/lcorolib.c \
	$LUA_DIR/lgc.c \
	$LUA_DIR/lobject.c \
	$LUA_DIR/ltable.c \
	$LUA_DIR/lvm.c
	$LUA_DIR/lctype.c \
	$LUA_DIR/linit.c \
	$LUA_DIR/lopcodes.c \
	$LUA_DIR/ltablib.c \
	$LUA_DIR/lzio.c
	$LUA_DIR/ldblib.c \
	$LUA_DIR/liolib.c \
	$LUA_DIR/loslib.c \
	$LUA_DIR/ltm.c \
	-o package/tangerine/tangerine.out
