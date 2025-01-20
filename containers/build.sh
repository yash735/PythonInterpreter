#!/bin/bash
#
# Build a container (image) with everything needed to compile and test
# our code.  The build and test operations are in the bash script
# build-and-test.sh.  Each Docker/Podman file should declare an
# ENTRYPOINT that runs this script.

dockerfile=$1
if [ "$dockerfile" == "" ]; then
    echo "Usage $0 <name-of-dockerfile> [fresh]"
    exit -1
fi

name=`basename $dockerfile`
cachearg=''
if [ "$2" == "fresh" ]; then
    echo 'Building a fresh image using --no-cache'
    cachearg='--no-cache'
fi

# Trying to fix clock skew error reported by 'make'
datetime=$(date +'%Y-%m-%dT%H:%M:%S')
podman machine ssh sudo date --set $datetime

# Build the image
# The namespace 'parse' is referenced in podrun.sh

cd ..

podman build --security-opt label=disable \
       --progress plain \
       $cachearg \
       -t parse:$name \
       -f "containers/$dockerfile" . 
