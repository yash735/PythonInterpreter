#!/bin/bash

# Ensure a .417 file is provided as an argument
if [[ $# -ne 1 || ! -f $1 ]]; then
    echo "Usage: ./run.sh <filename.417>"
    exit 1
fi

# Check if the parse command exists
if ! command -v ./parse &> /dev/null; then
    echo "Error: parse command not found in the current directory."
    exit 1
fi

# Run the parser and pass output to the interpreter
cat "$1" | ./parse | python3 integer_interpreter.py
