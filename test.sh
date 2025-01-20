#!/bin/bash
# Get the path to the parser
parse=$(command -v parse)

# EXAMPLE TEST: Test number parsing
$parse <<< '123' | ./run.sh
code=$?
if [[ $code -ne 0 ]]; then
    echo "Error"
    exit $code
fi

# Define a function for successful tests
function ok {
    $parse <<< ${@} | ./run.sh
    code=$?
    if [[ $code != 0 ]]; then
        echo "Interpreter failed with input: ${@}"
        exit $code
    fi
}

# Define a function for tests that should raise errors
function err {
    $parse <<< ${@} | ./run.sh
    code=$?
    if [[ $code == 0 ]]; then
        echo "Interpreter should have failed with input: ${@}"
        exit -1
    fi
}

# Test cases
ok '123'
ok 'x'
ok '"hi"'

# Test multiplication with two arguments
ok '(mul 2 3)'

# Test multiplication with multiple arguments
ok '(mul 2 3 4)'

# Test user-defined function with multiple arguments
ok '(lambda (a, b) { (add a b) } (5, 10))'

# Test blocks with multiple expressions
ok '{(add 2 3); (mul 4 5)}'

# Test factorial function
ok '{ def fact(n) { cond (zero? n => 1) (true => mul(n, fact(sub(n, 1)))) } (fact 5) }'

err '{123; 456}'

echo "All tests passed!"
