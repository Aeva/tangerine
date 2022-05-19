#!/bin/bash
cd `dirname $0`
cd ..
clang++ \
	-std=c++17 \
	-fPIC \
	-Ithird_party/fmt/include \
	-Ithird_party \
	-DMINIMAL_DLL \
	tangerine/sdfs.cpp \
	tangerine/profiling.cpp \
	tangerine/export.cpp \
	tangerine/magica.cpp \
	tangerine/threadpool.cpp \
	third_party/voxwriter/VoxWriter.cpp \
	third_party/fmt/src/format.cc \
	-shared -o package/tangerine/tangerine.so
