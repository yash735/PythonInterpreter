/*  -*- Mode: C; -*-                                                         */
/*                                                                           */
/*  ast.h   Abstract Syntax Tree for a traditional PL syntax                 */
/*                                                                           */
/*  (C) Jamie A. Jennings, 2024                                              */

#ifndef ast_h
#define ast_h

#include "lexer.h"
#include <stdbool.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* AST types and type names                                           */
/* ------------------------------------------------------------------ */

// The AST type names that are in all-caps are typically never
// part of the output.

#define _EXPS(X)					  \
       X(AST_FALSE,        "false")                       \
       X(AST_TRUE,         "true")                        \
       X(AST_IDENTIFIER,   "Identifier")                  \
       X(AST_INTEGER,      "INTEGER")                     \
       X(AST_STRING,       "STRING")		          \
       /* Declare all valid atoms above this line. */     \
       X(AST_NATOMS,       "SENTINEL")			  \
       /* Below: NULL, CONS, and various error types. */  \
       X(AST_NULL,         "NULL")			  \
       X(AST_CONS,         "CONS")                        \
       X(AST_PARAMETERS,   "Parameters")                  \
       X(AST_BLOCK,        "Block")                       \
       X(AST_APP,          "Application")                 \
       X(AST_DEFINITION,   "Def")                         \
       X(AST_LET,          "Let")                         \
       X(AST_ASSIGNMENT,   "Assignment")                  \
       X(AST_CLAUSE,       "Clause")			  \
       X(AST_COND,         "Cond")                        \
       X(AST_LAMBDA,       "Lambda")                      \
       X(AST_ERROR,        "ERROR")                       \
       X(AST_NTYPES,       "SENTINEL")
       
#define _FIRST(a, b) a,
typedef enum ast_type {_EXPS(_FIRST)} ast_type;
#undef _FIRST

#define _SECOND(a, b) b,
static const char *const AST_NAMES[] = {_EXPS(_SECOND)};
#undef _SECOND

/* ------------------------------------------------------------------ */
/* Error types and names                                              */
/* ------------------------------------------------------------------ */

#define _ERRORS(X)					 \
    X(ERR_ZERO,         "ZEROSENTINEL")		         \
    X(ERR_EOF,          "Unexpected EOF")	         \
    X(ERR_PROGRAM,      "Not a valid program")           \
    X(ERR_DEFINITION,   "Improper binding (def/let)")    \
    X(ERR_ASSIGNMENT,   "Improper assignment")           \
    X(ERR_LAMBDA,       "Improper lambda expression")    \
    X(ERR_PARAMETERS,   "Improper parameter list")       \
    X(ERR_BLOCK,        "Improper block")	         \
    X(ERR_COND,         "Improper cond")	         \
    X(ERR_COMMA,        "Expected comma")                \
    X(ERR_SEMICOLON,    "Expected semicolon")            \
    X(ERR_IDSYNTAX,     "Invalid identifier syntax")     \
    X(ERR_IDLEN,        "Identifier too long")           \
    X(ERR_INTSYNTAX,    "Invalid integer")               \
    X(ERR_INTLEN,       "Integer too long")              \
    X(ERR_INTRANGE,     "Integer out of range")          \
    X(ERR_STRCHAR,      "Invalid character in string")	 \
    X(ERR_STRESC,       "Invalid escape sequence")	 \
    X(ERR_STRLEN,       "String too long")               \
    X(ERR_BADCHAR,      "Illegal character")             \
    X(ERR_LEXER,        "Lexer failed")                  \
    X(ERR_NTYPES,       "SENTINEL")
       
#define _FIRST(a, b) a,
typedef enum error_type {_ERRORS(_FIRST)} error_type;
#undef _FIRST

#define _SECOND(a, b) b,
static const char *const ERROR_NAMES[] = {_ERRORS(_SECOND)};
#undef _SECOND

#define error_type_name(typ)						\
  (((0 <= (typ)) && (typ) < ERR_NTYPES) ? ERROR_NAMES[typ] : "INVALID")

#define error_name(a)							\
  (a									\
   ? (a->error ? error_type_name(a->error->type) : "NOT AN ERROR!")	\
   : "NULL AST")

/* ----------------------------------------------------------------------------- */
/* AST                                                                           */
/* ----------------------------------------------------------------------------- */

// The position of the error is 'start' in the main AST struct below
typedef struct ast_error_details {
  enum error_type    type;
  const char        *input;		// full input to parser
  char              *msg;		// optional message
} ast_error_details;

// Individual fields are valid only for the indicated types
typedef struct ast {
  enum ast_type  type;
  enum ast_type  subtype; // ONLY valid when type is CONS or NULL
  const char    *start;	  // ptr into 'input' to start of field
  union {
    struct {			// For CONS
      struct ast *car;
      struct ast *cdr;
    };
    ast_error_details *error;	// When ast_errorp() is true
    char              *str;	// For ID, BYTESTRING
    int64_t            n;	// For INT
  };
} ast;

/*
   NOTES about strings:

   (1) The string in TOKEN_STRING contains the delimiting double
   quotes, with its contents as written by the user, with escape
   sequences and no NUL bytes.

   (2) The string in AST_STRING does not contain the delimiting
   quotes, nor any escape sequences.  E.g. if the user writes "\n",
   the token struct will contain exactly that 4-byte string, but the
   conversion to AST_STRING produces a 1-byte string.  That one byte
   has numerical value 10 (ASCII for newline).

   (3) To print the contents of AST_STRING, we 'escape it', meaning we
   convert all bytes outside the printable ASCII range into a valid
   escape sequence.  (An escape sequence is valid if, when it is read,
   it produces the same byte value we started with.)  E.g. a string of
   one byte that is ASCII 10 could be printed as \n (2 bytes) or \x0A
   (4 bytes).

*/  

// Low-level constructor, destructor

ast *new_ast(enum ast_type type, const char *start);
void free_ast(ast *e);

// Accessors, mostly for convenience

const char *ast_name(ast *a);
const char *ast_type_name(ast_type type);
const char *ast_subtype_name(ast *a);
ast_type    ast_subtype(ast *a);

// Constructors

ast *ast_true(const char *start);
ast *ast_false(const char *start);
ast *ast_integer(const char *input, token tok);
ast *ast_string(const char *input, token tok);
ast *ast_identifier(token t);
ast *ast_error(enum error_type type,
	       const char *input,
	       const char *posn,
	       const char *msg);
ast *ast_error_tok(enum error_type type,
		   const char *input,
		   token tok);

// The usual for lists: build 'em up, tear 'em down

ast *ast_car(ast *list);
ast *ast_cdr(ast *list);
ast *ast_cons(enum ast_type kind, ast *item, ast *ls);
ast *ast_null(enum ast_type kind, const char *start);

// Predicates (ending in 'p' per Lisp tradition)

bool ast_nullp(ast *s);
bool ast_truep(ast *s);
bool ast_falsep(ast *s);
bool ast_consp(ast *s);
bool ast_listp(ast *s);	// cons or null
bool ast_atomp(ast *s);

#define MAKE_LIST_PREDICATE_DECL(name)		\
  bool name(ast *obj);

MAKE_LIST_PREDICATE_DECL(ast_applicationp)
MAKE_LIST_PREDICATE_DECL(ast_definitionp)
MAKE_LIST_PREDICATE_DECL(ast_letp)
MAKE_LIST_PREDICATE_DECL(ast_blockp)
MAKE_LIST_PREDICATE_DECL(ast_parametersp)
MAKE_LIST_PREDICATE_DECL(ast_lambdap)
MAKE_LIST_PREDICATE_DECL(ast_clausep)
MAKE_LIST_PREDICATE_DECL(ast_condp)

bool ast_identifierp(ast *obj);
bool ast_stringp(ast *obj);
bool ast_integerp(ast *obj);

// Exactly ONE of the following should return true for a given AST:
bool ast_formp(ast *obj);
bool ast_errorp(ast *s);

// The following predicates examine the *entire* list
bool ast_proper_listp(ast *obj);

// Utilities

void print_ast(ast *a);
void fprint_ast(FILE *f, ast *a);

void fprint_error(FILE *f, ast *a);
void print_error(ast *a);
error_type ast_error_type(ast *e);

ast *ast_nreverse(ast *ls);
int  ast_length(ast *ls);

ast *ast_copy(ast *a);		// deep copy
ast *ast_node_copy(ast *a);	// shallow copy

bool ast_equal(ast *a, ast *b);	     // deep comparison
bool ast_node_equal(ast *a, ast *b); // shallow comparison

/*
  To 'reduce' a list is to apply a binary (2-arg) function repeatedly
  until all the values in the list have been consumed.  An initial
  value is combined with the first item in the list to produce a
  result.  That result is then combined with the next item in the
  list, and so on.  The function that combines 2 items is a 'reducer'
  and it has the type

     fn(<R>, <T>) --> <R>

  where <R> is the result type and <T> is the type of items in the
  list.  Note that the initial value must have type <R>.

  This operation is now more commonly called 'fold' or 'foldl'
  (because it evaluates 'fn' in a left-associative fashion).

*/

typedef void *astReducer(void *acc, ast *s);
void *ast_reduce(astReducer *fn, void *initial_value, ast *ls);

/*
  To 'map' over a list is to apply a unary (1-arg) function to each
  element of the new list, producing a new list with the results.
  Here, we restrict the type of the map function to be ast -> ast.
 */

typedef ast *astMapper(ast *a);
ast *ast_map(astMapper *fn, ast *ls);


#endif
