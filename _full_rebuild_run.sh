#!/bin/bash
# [DA_PORT] one-shot: reconfigure + full rebuild with new explicit image bases
# Absolute MSYS2 paths - this script may run under Git Bash, where /mingw64 is Git's own
export PATH=/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH
cd "/d/Dead Air/xray-16/build_mingw" || { echo BUILD_FAILED_CD > _full_rebuild.log; exit 1; }
: > _full_rebuild.log
cmake . >> _full_rebuild.log 2>&1 || { echo BUILD_FAILED_CONFIGURE >> _full_rebuild.log; exit 1; }
cmake --build . -j8 >> _full_rebuild.log 2>&1 && echo BUILD_DONE_OK >> _full_rebuild.log || echo BUILD_FAILED >> _full_rebuild.log
