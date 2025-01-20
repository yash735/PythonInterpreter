#!/bin/bash

# (-e) exit if a command gives an error, (-o pipefail) even if in a
# pipe, (-u) exit if using a variable that is undefined
set -euo pipefail

# To start the podman daemon running:
# (Once, to get an initial vm)      podman machine init
# (Whenever you need to use podman) podman machine start
# (To stop the podman service)      podman machine stop

if [ -z "$1" ]; then
    echo Usage: $0 dockerfilename [fresh]
    echo   where the optional 'fresh' option clears the
    echo   cache and builds a completely fresh image
fi

# Build
./build.sh "$@"

# Run the ENTRYPOINT command established in the build
name=`basename $1`
podman run parse:$name
