#!/bin/bash
# 
#  clitest.sh
# 
#  (C) Jamie A. Jennings, 2024

set -eu
set -o pipefail

allpassed=1
function contains {
    for str in "$@"; do
	if [[ "$output" != *"$str"* ]]; then
	    printf "Output did not contain '%s'\n" "$str"
	    allpassed=0
	fi
    done
}


fact='def fact = λ(n) {cond (zero?(n) => 1) (true => mul(n, fact(sub(n, 1))))}'

./parse <<< "$fact" >/dev/null

./parse >/dev/null <<EOF
// Factorial
def fact = λ(n)
  {
  cond 
    (zero?(n) => 1) 
    (true => mul(n, fact(sub(n, 1))))
  }
// And there can be comments at the end
EOF

expected_factorial='{"Def":[{"Identifier": "fact"},{"Lambda":[{"Parameters":[{"Identifier": "n"}]},{"Block":[{"Cond":[{"Clause":[{"Application":[{"Identifier": "zero?"},{"Identifier": "n"}]},1]},{"Clause":[{"Identifier": "true"},{"Application":[{"Identifier": "mul"},{"Identifier": "n"},{"Application":[{"Identifier": "fact"},{"Application":[{"Identifier": "sub"},{"Identifier": "n"},1]}]}]}]}]}]}]}]}'

expected_factorial_sexp='(Def fact (Lambda (n) (Block (Cond ((zero? n) 1) (true (mul n (fact (sub n 1))))))))'

json=$(./parse <<< "$fact")

if [[ "$json" != "$expected_factorial" ]]; then
    echo "Factorial test failed!"
    exit -1
else
    echo "Factorial test passed"
fi

sexpression=$(./parse -s <<< "$fact")

if [[ "$sexpression" != "$expected_factorial_sexp" ]]; then
    echo "Factorial test (s-expression) failed!"
    exit -1
else
    echo "Factorial test (s-expression) passed"
fi

output=$(./parse -k)
contains "λ"
contains "lambda"
contains "=>"
contains "="
contains "let"
contains "def"
contains "cond"

if [[ $allpassed -ne 1 ]]; then
    echo "Keyword output test failed!"
    exit -1
else
    echo "Keyword output test passed"
fi


output1=$(./parse <<< "{def foo = 1; let bar = 2; def baz = 3; 444}")
expected_defs1='{"Block":[{"Def":[{"Identifier": "foo"},1]},{"Let":[{"Identifier": "bar"},2,{"Block":[{"Def":[{"Identifier": "baz"},3]},444]}]}]}'

output2=$(./parse -a <<< "{def foo = 1; let bar = 2; def baz = 3; 444}")
expected_defs2='{"Block":[{"Def":[{"Identifier": "foo"},{"Number": 1}]},{"Let":[{"Identifier": "bar"},{"Number": 2},{"Block":[{"Def":[{"Identifier": "baz"},{"Number": 3}]},{"Number": 444}]}]}]}'


output3=$(./parse -s <<< "{def foo = 1; let bar = 2; def baz = 3; 444}")
expected_defs3='(Block (Def foo 1) (Let bar 2 (Block (Def baz 3) 444)))'


if [[ "$output1" != "$expected_defs1" ]]; then
    echo "Def/Let test 1 failed!"
    exit -1
fi
if [[ "$output2" != "$expected_defs2" ]]; then
    echo "Def/Let test 2 failed!"
    exit -1
fi
if [[ "$output3" != "$expected_defs3" ]]; then
    echo "Def/Let test 3 failed!"
    exit -1
fi

echo "Def/Let test passed"

