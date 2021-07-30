#!/bin/bash
cd `dirname $0`
cd ..
clang++ \
	-std=c++14 \
	-fPIC \
	-Ithird_party \
	-lGL \
	backend/*.cpp \
	third_party/glad/glad.c \
	third_party/glad/glad_glx.c \
	-shared -o tangerine.so
