#!/bin/bash
version=$1
version_define="\"$version\""

rm -rf release/macos
mkdir -p release/macos

CPP='g++ -std=c++11 -Werror -Wall -Wno-unused-variable -g -O2 -I.'
$CPP -DVERSION=$version_define build.cc -o release/macos/build_macos_$version -pthread

rm -rf release/macos/*.dSYM
