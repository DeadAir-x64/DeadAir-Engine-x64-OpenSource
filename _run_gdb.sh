#!/bin/bash
export PATH="/mingw64/bin:/usr/bin:$PATH"
cd "/d/Dead Air/Dead Air"
cat > /tmp/gdb_cmds.txt << 'EOF'
set pagination off
set confirm off
run -force_flushlog
bt full
info registers
quit
EOF
gdb -batch -x /tmp/gdb_cmds.txt ./bin/xrEngine.exe 2>&1 | tail -80