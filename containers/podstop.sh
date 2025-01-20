#!/bin/bash
#

# To start the podman daemon running:
# (Once, to get an initial vm)      podman machine init
# (Whenever you need to use podman) podman machine start
# (To stop the podman service)      podman machine stop

# ~/.config/containers/containers.conf must have this:
#   [containers]
#   tz = "local"

podman machine stop



