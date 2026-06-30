#!/bin/bash
export PATH="/mingw64/bin:/usr/bin:$PATH"
cd "/d/Dead Air/xray-16/build_mingw"
cmake --build . -j8 2>&1 | tail -20