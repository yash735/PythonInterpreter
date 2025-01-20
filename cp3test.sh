#!/bin/bash

# Define the command that runs the parser
# --> Change this as needed.
parse="$(command -v parse)"

# ------------------------------------------------------------------

# Use this function when we expect the interpreter to succeed, and to
# make sure it returns the right answer.  The only output should be
# the value produced by the input program.
function ok {
    output=$($parse < "$1" | ./run.sh)
    code=$?
    if [[ $code != 0 ]]; then
        echo "Interpreter returned error code for input file $1"
        echo "Expected success and output $2"
        exit $code
    fi
    if [[ "$output" != "$2" ]]; then
        echo "Interpreter produced unexpected output: $output"
        echo "Expected this output: $2"
        exit 1
    fi
}

# Use this function when you expect the interpreter to fail.  The
# output is not checked because it should be a human-readable message,
# but the status code must be non-zero.
function err {
    $parse < "$1" | ./run.sh
    code=$?
    if [[ $code == 0 ]]; then
        echo "Interpreter (and parser) succeeded but should not have!"
        echo "Input file was $1"
        exit 1
    fi
}

ok cp3ex1.417 18
ok cp3ex2.417 1
ok cp3ex3.417 90
ok cp3ex4.417 3628800

echo "All tests passed!"
