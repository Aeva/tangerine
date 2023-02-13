#!/bin/bash
cd `dirname $0`
cd ..

cmake -B linux/build/Debug -DCMAKE_BUILD_TYPE=Debug
cmake -B linux/build/Release -DCMAKE_BUILD_TYPE=Release

exec cmake --build linux/build/Release
