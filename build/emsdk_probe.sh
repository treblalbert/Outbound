#!/bin/sh
EMSDK=/c/emsdk
for p in "$EMSDK/python/3.13.3_64bit/python.exe" "$EMSDK/python/3.13.3_64bit/bin/python3" "$EMSDK/python/3.9.2_64bit/bin/python3"; do
  if [ -f "$p" ]; then EMSDK_PYTHON="$p"; break; fi
done
export EMSDK EMSDK_PYTHON
echo "python: $EMSDK_PYTHON"
EMSDK_QUIET=1
export EMSDK_QUIET
. "$EMSDK/emsdk_env.sh"
echo "source_status=$?"
echo "empp=$(command -v em++)"
