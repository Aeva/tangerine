#!/bin/bash
cd `dirname $0`
cd ..
clang++ \
	-std=c++20 \
	-fPIC \
	-Ithird_party \
	-DMINIMAL_DLL \
	tangerine/sdfs.cpp \
	tangerine/profiling.cpp \
	tangerine/magica.cpp \
	-shared -o package/tangerine/tangerine.so
