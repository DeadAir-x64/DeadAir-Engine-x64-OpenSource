#!/bin/bash
export PATH="/mingw64/bin:/usr/bin:$PATH"
cd "/d/Dead Air/xray-16/build_mingw"
cmake --build . --target xrCore -j8 2>&1 | tail -10
echo "--- xrCore done, building xrGame ---"
cmake --build . --target xrGame -j8 2>&1 | tail -10