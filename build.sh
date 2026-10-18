#!/bin/sh
# Linux / macOS build without CMake:  ./build.sh && ./build/giftshop
mkdir -p build
cc -O2 -c third_party/sqlite3.c -Ithird_party -o build/sqlite3.o || exit 1
c++ -std=c++17 -O2 -Ithird_party -Isrc src/main.cpp build/sqlite3.o -o build/giftshop -lpthread -ldl || exit 1
echo "Built. Run:  ./build/giftshop   then open http://localhost:8080"
