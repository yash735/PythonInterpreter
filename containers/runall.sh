#!/bin/bash

# (-e) exit if a command gives an error, (-o pipefail) even if in a
# pipe, (-u) exit if using a variable that is undefined
set -euo pipefail

declare -a OSes=(debian
		 manjaro
		 rocky
		 rocky-ulimit
		 ubuntu
		 ubuntu-ulimit
		 fedora)

for os in ${OSes[@]}; do
    echo "RUNNING CONTAINER $os"
    ./podrun.sh "$os"
done

