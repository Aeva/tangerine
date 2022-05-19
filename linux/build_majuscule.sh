#!/bin/bash
cd `dirname $0`
cd ..

clang++ \
	-std=c++17 \
	-fPIC \
	-U_WIN64 \
	-Ithird_party/fmt/include \
	-Ithird_party/imgui \
	-Ithird_party/imgui/backends \
	-Ithird_party \
	-I/usr/include/racket \
	-L/usr/lib/racket \
	-lracket \
	`pkg-config --libs --cflags sdl2 gtk+-3.0` \
	tangerine/*.cpp \
	third_party/imgui/*.cpp \
	third_party/imgui/backends/imgui_impl_opengl3.cpp \
	third_party/imgui/backends/imgui_impl_sdl.cpp \
	third_party/fmt/src/format.cc \
	third_party/Glad/glad.c \
	third_party/voxwriter/VoxWriter.cpp \
	-o package/tangerine/tangerine.out
