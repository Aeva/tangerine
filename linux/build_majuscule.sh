#!/bin/bash
cd `dirname $0`
cd ..

cmake -B linux/build/Debug -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_COMPILER=clang
cmake -B linux/build/Release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_COMPILER=clang

cmake --build linux/build/Release
