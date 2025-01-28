# csc417

Parser for a simple expression-based programming language.

## Getting started

Clone this repository, then build the parser using one of these `make` commands:

* To build in debug/development mode: `make`
* To build in release mode (not necessary): `make release`
* To install to default location of `/usr/local`: `sudo make install`
* To install to a custom location, e.g. `/some/path/bin`: `sudo make DESTDIR=/some/path install`

The parser reads from the standard input.  See below for the grammar that
defines the input.  The simplest test is probably parsing an integer.  Here we
use the bash operator `<<<` that takes the remainder of the command line and
supplies it as input to the program:

```shell
$ ./parse <<< 123
123
$ 
```

We could instead use `echo` (or `printf`) to supply input:

```shell
$ echo "-123" | parse
-123
$ 
```

Later, when our programs get longer than just one number, we will put each
program into a file, e.g.

```shell
$ parse < fact.417 | interp
120
$ 
```

It's a good habit to quote program input on the command line so that the shell
does not try to interpret it.  For example:

```shell
$ ./parse <<< f(1)
bash: syntax error near unexpected token `('
$
$ ./parse <<< 'f(1)'
{"Application":["f",1]}
$
```

The default output format is JSON.  You can pretty-print (and query and
transform) the JSON using a program like `jq`.

```shell
$ ./parse <<< 'f(1)' | jq
{
  "Application": [
    "f",
    1
  ]
}
$ 
```

The parser output goes to the standard output, as shown above where the parsed
output is piped to `jq`.  Of course, it is more interesting to pipe the output
to an interpreter.

## Useful (perhaps) output options

The `-a` option always outputs a JSON object, even for numbers and strings.
Rationale: Some JSON libraries (like serde for Rust) make it easy to destructure
JSON into a data structure (an `enum` for Rust, a struct for Go, etc.) if every
input is a JSON object.  Those libraries may make it awkward to check for the
other kinds of JSON values: number, string, array, true, false, or null.

```shell
$ parse <<< "123"
123
$ parse -a <<< "123"
{"Number": 123}
$ 
```

The `-s` option outputs S-expressions instead of JSON.  Identifiers are not
labeled as such: they are simply unquoted values (symbols).  Applications are
not labeled, either: they look like applications in Scheme or Lisp.  Blocks are
labeled, similar to Scheme's `begin` keyword.

```shell
$ parse <<< "foo"
{"Identifier": "foo"}
$ parse -s <<< "foo"
foo
$ 
$ parse <<< "f(x)"
{"Application":[{"Identifier": "f"},{"Identifier": "x"}]}
$ parse -s <<< "f(x)"
(f x)
$
$ parse <<< "{f(x);g(x)}"
{"Block":[{"Application":[{"Identifier": "f"},{"Identifier": "x"}]},{"Application":[{"Identifier": "g"},{"Identifier": "x"}]}]}
$ parse -s <<< "{f(x);g(x)}"
(Block (f x) (g x))
$ 
```

## Grammar for the concrete syntax

The syntax is designed for an expression-oriented language.  Every program is an
expression.  Note that there is some de-sugaring done by the parser before the
AST is output.  In other words, the AST produced is, in certain cases, different
from this concrete syntax (see below).

```
PROGRAM := EXP

EXP := FORM | ATOM

FORM := APPLICATION
      | LAMBDA
      | COND
      | BLOCK
      | LET
      | DEFINITION
	  | ASSIGNMENT

ATOM := IDENTIFIER
      | STRING
      | INTEGER

// Forms

APPLICATION := EXP '(' ARGLIST? ')'
LAMBDA := ('lambda' | 'λ') '(' PARAMETERS ')' BLOCK
COND := 'cond' CLAUSE+
BLOCK := '{' EXPLIST? '}'
LET := 'let' IDENTIFIER '=' EXP BLOCK?
DEFINITION := 'def' IDENTIFIER '=' EXP
ASSIGNMENT := IDENTIFIER '=' EXP

PARAMETERS := IDENTIFIER (',' IDENTIFIER)*
ARGLIST := EXP (';' EXP)*
CLAUSE := '(' EXP '=>' EXP ')'

// Atoms
     
IDENTIFIER := IDSTART IDCHAR*             // See restriction below
IDSTART := ICHAR except for DIGIT and PLUS and MINUS
IDCHAR := UTF8 except for DELIMITERS
DELIMITERS := WS | '"' | '(' | ')' | '{' | '}' | ',' | ';'
     
STRING := '"' (UTF8NOBS | ESCAPESEQ)* '"'
UTF8NOBS := UTF8 except for backslash ('\' codepoint 92)
ESCAPESEQ := '\' ('\' | '"' | 't' | 'n' | 'r')

INT := ('+' | '-')? DIGIT+
DIGIT := '0' | '1' | '2' | '3' | '4' | '5' | '6' | '7' | '8' | '9'

UTF8 := Any Unicode character (codepoint) encoded in UTF-8
WS := space (ASCII 32) | tab (9) | return (13) | newline (10)

// Comments and whitespace

Comments begin with '//' and extend to the end of the line (\n).
The parser is insensitive to whitespace and comments.
   
// Restriction on identifiers

An identifier cannot be a keyword.  Keywords can be found in the 
grammar above, and include: lambda, λ, cond, def, let, =, =>

```

Other restrictions:

* The interpretation of an integer literal must be a valid signed 64-bit value.
* String literals may contain any byte sequence.  The escape character is
  backslash, and it is needed to denote: escape `\\`, double quote `\"`, tab
  `\t`, newline `\n`, and return `\r`.  and tab.
* Unprintable 7-bit ASCII, e.g. DEL and NUL, are not representable.
* UTF-8 is a superset of ASCII and should work fine.

More examples:

```
$ ./parse <<< 10002000300040005000
Error: [Integer out of range] 
  10002000300040005000
  ^
$ ./parse <<< '"abc\n"'
"abc\n"
$ ./parse <<< '"☃"'
"☃"
$ ./parse <<< '"abc\x"'
Error: [Invalid escape sequence] 
  "abc\x"
  ^
$ 
```

## Syntactic sugar

As of version 1.0.1, the only de-sugaring step is one that transforms Blocks
(expression sequences) containing `let` expressions.

There are two distinct ways to write a `let` expression in the 417 language.
The first is a form that has 3 parts: the identifier to be bound, an expression
that produces the value, and a code block.

This use reminds one of `let` expressions in Scheme and OCaml.

The parser supports this syntax to facilitate a semantics in which the scope of
the bound variable is the code block.  For example, the `let` form below
introduces a binding for `incr` that is visible only within the code block
containing the call to `print`:

```
let incr = λ(n) {add(1, n)} {print(incr(5))}
```

Unlike Scheme's syntax for `let`, the 417 syntax allows only one variable to be
bound.  As a convenience to the programmer, the 417 grammar allows a `let` form
to omit the code block.  The programmer can write, for example:

```
{
  let amt = 1;
  let incr = λ(n) {add(amt, n)};
  print(incr(5))
}
```

This syntax is reminiscent of the `let` _statement_ in OCaml, which has this
semantics: the scope of the bound variable is the remainder of the code block.
In the example above, there are 3 expressions in the code block.  Following the
OCaml model, the scope of `amt` would be the rest of the block, i.e. the let of
`incr` and the `print` expression.

While parsing does not prescribe any particular semantics, it can strongly favor
some over others.  Our 417 language de-sugars each use of the block-less `let`
by transforming it into a `let` with a code block.  In this way, the parser
greatly facilitates a semantics for `let` that resembles that of OCaml, or for
that matter, Rust.  And though languages like C and Java do not use the `let`
keyword, those languages introduce new bindings with `let`-like statements,
e.g. `int i = 0;` in C.

Using the AST tree printer feature of the parser, we see that: (1) each `let` in
the AST has 3 parts (identifier, expression, block); and (2) the code block for
`let` statement is the rest of the block in which the `let` statement appears:

```shell 
$ ./parse -t <<< '{let amt = 1; let incr = λ(n) {add(amt, n)}; print(incr(5))}'
Block
└── Let
    ├── amt
    ├── 1
    └── Block
        └── Let
            ├── incr
            ├── Lambda
            │   ├── Parameters
            │   │   └── n
            │   └── Block
            │       └── Application
            │           ├── add
            │           ├── amt
            │           └── n
            └── Block
                └── Application
                    ├── print
                    └── Application
                        ├── incr
                        └── 5

$ 
```

## Example of LAMBDA expression syntax

As you know, lambda denotes a function type.  The keyword `lambda` (which can
also be written `λ`) introduces a _function literal_, also called an _anonymous
function_.  The keyword must be followed by a parameter list and then a function
body.

The parameter list is made of zero or more identifiers.  Here is a function of
no arguments that returns the number 1, with pretty-printing via `jq`:

```shell
$ ./parse <<< 'λ() {1}'
{"Lambda":[{"Parameters":[]},{"Block":[1]}]}
$
$ ./parse <<< 'λ() {1}' | jq
{
  "Lambda": [
    {
      "Parameters": []
    },
    {
      "Block": [
        1
      ]
    }
  ]
}
$ 
```

## Example of function application

The identity function, written as a literal anonymous function, applied to the
integer argument `9`:

```shell
$ ./parse <<< 'f(x)'
{"Application":[{"Identifier": "f"},{"Identifier": "x"}]}
$ 
$ ./parse <<< 'λ(x) {x} (9)'
{"Application":[{"Lambda":[{"Parameters":[{"Identifier": "x"}]},{"Block":[{"Identifier": "x"}]}]},9]}
$ 
$ ./parse <<< 'λ(x) {x} (9)' | jq
{
  "Application": [
    {
      "Lambda": [
        {
          "Parameters": [
            {
              "Identifier": "x"
            }
          ]
        },
        {
          "Block": [
            {
              "Identifier": "x"
            }
          ]
        }
      ]
    },
    9
  ]
}
$ 
```


## Example of factorial program

A definition of the ordinary recursive factorial calculation might look like the
example below.

The output from `jq`, below, is very long vertically and was edited by hand.
Comments were inserted to illustrate the split within each `cond` clause between
the test and the consequent.  The program is [here](factorial.417).

```shell
$ ./parse < factorial.417 | jq
{ "Def": [ { "Identifier": "fact" },
           { "Lambda": [ { "Parameters": [ { "Identifier": "n" } ] },
                         { "Block": [ 
                            { "Cond": [ 
                               { "Clause": [ 
                                  { "Application": [ 
                                     { "Identifier": "zero?" }, 
                                     { "Identifier": "n" } ] },
                                  // =>
                                  1 ] },
                               { "Clause": [
                                  { "Identifier": "true" },
                                  // =>
                                  { "Application": [ 
                                     { "Identifier": "mul" }, 
                                     { "Identifier": "n" }, 
                                     { "Application": [ 
                                        { "Identifier": "fact" },
                                        { "Application": [
                                           { "Identifier": "sub" },
                                           { "Identifier": "n" },
                                           1 ] } ] } ] } ] } ] } ] } ] } ] }
$ 
```


## Parser options

* `-k` to list the keywords.  These will never be parsed as identifiers.
* `-t` to output a tree representation of the input.
* `-a` to **always** output a JSON object, even for numbers and strings.
* `-s` to change output format to [S-expressions](https://en.wikipedia.org/wiki/S-expression).
* `-v` to print the parser version. 
* `-h` for help. 


## Interpretor:
The code that i wrote is in this, file, basically it an python interpretor for the programming language called 417 (made by us).
Whata ever code that is written in file.417 will be interpreted using this python file and the parser. ONLY THE PYHTON FILE IS WRITTEN BY ME.

## License

The code in this repository is licensed under the MIT open source licence.  This
project is meant for educational use.  It has _not_ been designed for broad
platform support, easy extensibility, or good performance (to name just a few of
the non-requirements).




