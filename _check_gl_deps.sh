#!/bin/bash
export PATH="/mingw64/bin:/usr/bin:$PATH"
ldd "/d/Dead Air/Dead Air/bin/xrRender_GL.dll" 2>&1 | grep -i "not found"
echo "---"
ldd "/d/Dead Air/Dead Air/bin/xrRender_GL.dll" 2>&1 | grep -i "opengl\|SDL\|glext\|GL"