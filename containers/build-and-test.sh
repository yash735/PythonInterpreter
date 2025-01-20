#!/bin/bash

# BUILD AND TEST IN A PODMAN/DOCKER CONTAINER

# Exit immediately if a command does not succeed
set -e

cd /opt/project

# -------------------------------------------------------
# Unit test builds (all debugging enabled)
# -------------------------------------------------------

make clean
make debug
make test

# -------------------------------------------------------
# Production build
# -------------------------------------------------------

make clean
make release

# If argument given, then we want to limit available virtual memory
# and enable tracing of allocations.  The address sanitizer used in
# the debug build will not work under ulimit -v.

# Optional first argument is ulimit -v argument (in Kb)
if [[ -n "$1" ]]; then
    ulimit -v "$1"
    ulimit -a
    export CSC417_TRACE=1
fi

make test
