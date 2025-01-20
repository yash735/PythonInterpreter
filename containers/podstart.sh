#!/bin/bash
#

# To start the podman daemon running:
# (Once, to get an initial vm)      podman machine init
# (Whenever you need to use podman) podman machine start
# (To stop the podman service)      podman machine stop

# To increase the disk size available to the vm that runs podman:
#   podman machine rm
#   podman machine init --disk-size=100
# The disk size is in GiB.

# ~/.config/containers/containers.conf must have this:
#   [containers]
#   tz = "local"

# Default memory seems to be 2GB, and disk 100GB
memoryMB=3000
diskGB=250

echo NOTE: Stopping current machine, if one is running
podman machine stop
echo NOTE: Setting machine memory to $memoryMB MB
podman machine set --memory $memoryMB
echo NOTE: Setting machine disk to $diskGB GB
podman machine set --disk-size $diskGB
echo NOTE: Starting default machine
podman machine start

# E.g.
# export DOCKER_HOST=
# 'unix:///Users/jennings/.local/share/containers/podman/machine/qemu/podman.sock'

dir=$(podman machine info | awk '/machineimagedir:/ {print $2}')
dir+="/podman.sock"
dir="unix://${dir}"
echo NOTE: Setting DOCKER_HOST to: $dir
export DOCKER_HOST=${dir}



