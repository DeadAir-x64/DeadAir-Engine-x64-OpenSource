#!/bin/bash
# [DA_PORT] Deploy the FULL x64 binary set to all three installs.
# Partial deploys (xrGame.dll only) mix module vintages and broke startup once
# (pseudo-reloc layout mismatch) - always ship the whole set. See
# PORT_x64_BINDING_GAPS.md "КОРНЕВАЯ ПРИЧИНА НЕСТАБИЛЬНОГО СТАРТА".
# If a target file is locked (game running / stuck), it is renamed aside first -
# NTFS allows renaming mapped images.
set -u
SRC="/d/Dead Air/xray-16/bin/AMD64/Release"
INSTALLS=("/d/Dead Air Test/Dead Air" "/d/Dead Air x64" "/d/Dead Air x64 Clean")

fail=0
for inst in "${INSTALLS[@]}"; do
    echo "=== $inst"
    for f in "$SRC"/*.dll "$SRC"/xr_3da.exe; do
        name=$(basename "$f")
        dst="$inst/$name"
        if ! cp "$f" "$dst" 2>/dev/null; then
            mv "$dst" "$dst.locked.$$" 2>/dev/null
            if cp "$f" "$dst" 2>/dev/null; then
                echo "  $name: replaced (old renamed aside)"
            else
                echo "  $name: FAILED"
                fail=1
            fi
        fi
    done
    rm -f "$inst"/*.locked.* 2>/dev/null
done
[ $fail -eq 0 ] && echo "DEPLOY OK" || echo "DEPLOY HAD FAILURES"
