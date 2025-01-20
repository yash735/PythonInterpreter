#  -*- Mode: AWK//l; -*-                                                  
# 
#  interp.awk
# 
#  (C) Jamie A. Jennings, 2024

BEGIN {
    env["x"] = 10; env["v"] = 5; env["i"] = 1
}

{
    if ($1 ~ /^[:space:]*{"Application":.*}/) {
	print "Error: function application not implemented yet"
	exit(1)
    } 
    if ($1 ~ /^[:space:]*{"Block":.*}/) {
	print "Error: code blocks not implemented yet"
	exit(1)
    } 
    status = match($0, /^[:space:]*{"Identifier": "(.*)"}/, name)
    if (status != 0) {
	if (name[1] in env) {
	    print env[name[1]]
	    exit(0)
	} else {
	    print "Error: unbound identifier: ", $1
	    exit(1);
	}
    }
    # Test for number
    if ($1 + 0 == $1) {
	print $1
	exit(0)
    }
    # Test for string literal
    if ($1 ~ /^[[:space:]]*".*"/) {
	print $1
	exit(0)
    }
    print "Error: unknown expression: " $0
    exit(1)
}

	


