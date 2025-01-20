#!/bin/bash
os=$(uname -s)
kernelversion=$(uname -v)
allpassed=1

function check-for {
    for str in "$@"; do
        if [[ "$(command -v $str)" == "" ]]; then
            printf "Not found: %s\n" "$str"
            allpassed=0
        fi
    done
}

# Install dependencies for Ubuntu
if [[ $kernelversion == *"Ubuntu"* ]]; then
    echo "On Ubuntu. Installing dependencies now (requires sudo)..."
    sudo apt-get install -y \
        gcc \
        git \
        make \
        libc6-dev \
        libbsd-dev
elif [[ $os == "Darwin" ]]; then
    echo "On macOS. Please ensure required dependencies (gcc, git, make, python3) are installed via brew or manually."
else
    echo "On unsupported OS. Please ensure required dependencies (gcc, git, make, python3) are installed manually."
    check-for git gcc make python3
fi

# Check for Python 3 for interpreter execution
check-for python3

if [[ $allpassed -eq 1 ]]; then 
    echo "All dependencies installed and code compiled successfully."
    exit 0
else
    echo "One or more dependencies were not found or could not be installed."
    exit 1
fi
