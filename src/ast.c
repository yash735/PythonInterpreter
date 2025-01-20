/*  -*- Mode: C; -*-                                                         */
/*                                                                           */
/*  ast.c   Abstract Syntax Tree for a traditional PL syntax                 */
/*                                                                           */
/*  (C) Jamie A. Jennings, 2024                                              */

#include "ast.h"
#include "lexer.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static bool quotep(const char *s) {
  return s && (*s == '"');
}

const char *ast_type_name(ast_type type) {
  return (((0 <= (type)) && (type) < AST_NTYPES)
	  ? AST_NAMES[type]
	  : "INVALID");
}

const char *ast_name(ast *a) {
  return ast_type_name(a->type);
}

ast_type ast_subtype(ast *a) {
  return a->subtype;
}

const char *ast_subtype_name(ast *a) {
  ast_type type = ast_subtype(a);
  if ((type == AST_PARAMETERS)
      || (type == AST_BLOCK)
      || (type == AST_APP)
      || (type == AST_LET)
      || (type == AST_DEFINITION)
      || (type == AST_ASSIGNMENT)
      || (type == AST_LAMBDA)
      || (type == AST_COND)
      || (type == AST_CLAUSE))
    return ast_type_name(type);
  PANIC("Unhandled list subtype %s", ast_type_name(type));
}

// -----------------------------------------------------------------------------
// Constructors
// -----------------------------------------------------------------------------

// Low-level constructor, populates only the common fields
ast *new_ast(enum ast_type type, const char *start) {
  // Could replace with bump allocator if performance becomes an issue
  ast *e = xmalloc(sizeof(ast));
  if (!e) PANIC_NULL();
  e->type = type;
  e->subtype = -1;		// Uninitialized
  e->start = start;
  return e;
}

ast *ast_null(enum ast_type kind, const char *start) {
  ast *a = new_ast(AST_NULL, start);
  a->subtype = kind;
  a->start = start;
  return a;
}

ast *ast_true(const char *start) {
  return new_ast(AST_TRUE, start);
}

ast *ast_false(const char *start) {
  return new_ast(AST_FALSE, start);
}

// Syntax error object
// Message field is optional
ast *ast_error(enum error_type type,
	       const char *input,
	       const char *posn,
	       const char *msg) {
  ast *e = new_ast(AST_ERROR, posn);
  ast_error_details *deets = xmalloc(sizeof(ast_error_details));
  if (!deets) PANIC_OOM();
  e->error = deets;
  deets->type = type;
  deets->input = input;
  deets->msg = msg ? strndup(msg, MAX_MSGLEN) : NULL;
  return e;
}

// Convenience
ast *ast_error_tok(enum error_type type,
		   const char *input,
		   token tok) {
  return ast_error(type, input, tok.start + tok.pos, NULL);
}

// Number may be syntactically correct but not representable in an i64
ast *ast_integer(const char *input, token tok) {
  int64_t n;
  if (!interpret_int(tok.start, tok.len, &n))
    return ast_error_tok(ERR_INTRANGE, input, tok);
  ast *e = new_ast(AST_INTEGER, tok.start);
  e->n = n;
  return e;
}

// A TOKEN_STRING contains the opening and closing quotation marks, so
// its length is always at least 2.  We strip these off and unescape
// when we create the AST_STRING.
//
// ASCII-only, for now.
//
ast *ast_string(const char *input, token tok) {
  if (tok.type != TOKEN_STRING) PANIC("Not a string token");

  const char *err;
  char *str = unescape(tok.start + 1, token_length(tok), quotep, &err);

  if (str) {
    assert(quotep(err));
    assert(err == tok.start + tok.len - 1);
    ast *e = new_ast(AST_STRING, tok.start);
    e->str = str;
    return e;
  }

  if (!err)
    return ast_error_tok(ERR_STRLEN, input, tok);

  if (*err == ESC) {
    ssize_t pos = err - tok.start;
    assert((pos >= 0) && (pos <= UINT16_MAX));
    return ast_error_tok(ERR_STRESC, input, tok);
  }

  // The lexer should have caught this
  if (*err == '\0') PANIC("unexpected eof");

  PANIC("unhandled situation");
}

ast *ast_identifier(token tok) {
  size_t len = token_length(tok);
  if (!tok.start || (len == 0))
    PANIC("Invalid contents of identifier token");
  ast *e;
  char *str = xmalloc(len + 1);
  if (!str) PANIC_OOM();
  memcpy(str, tok.start, len);
  str[len] = '\0';
  e = new_ast(AST_IDENTIFIER, tok.start);
  e->str = str;
  return e;
}

// -----------------------------------------------------------------------------
// Predicates
// -----------------------------------------------------------------------------

#define AST_PREDICATE(predname, asttype)	\
  bool predname(ast *obj) {			\
    if (!obj) PANIC_NULL();			\
    return (obj->type == (asttype));		\
  }

AST_PREDICATE(ast_stringp, AST_STRING);
AST_PREDICATE(ast_identifierp, AST_IDENTIFIER);
AST_PREDICATE(ast_integerp, AST_INTEGER);
AST_PREDICATE(ast_consp, AST_CONS);
AST_PREDICATE(ast_nullp, AST_NULL);
AST_PREDICATE(ast_truep, AST_TRUE);
AST_PREDICATE(ast_falsep, AST_FALSE);
AST_PREDICATE(ast_errorp, AST_ERROR);

// This predicate does not examine the entire list, so it cannot tell
// if the list is well-formed.
bool ast_listp(ast *obj) {
  return ast_nullp(obj) || ast_consp(obj);
}

static bool ast_atom_typep(enum ast_type t) {
  return (t >= 0) && (t < AST_NATOMS);
}

// The list of S-expression types has all the atomic values at the top.
bool ast_atomp(ast *obj) {
  return ast_atom_typep(obj->type);
}

bool ast_formp(ast *obj) {
  return (ast_atomp(obj) || ast_listp(obj));
}

#define MAKE_LIST_PREDICATE(name, type) 		\
  bool name(ast *obj) {					\
    return ast_listp(obj) && (obj->subtype == (type));	\
  }

MAKE_LIST_PREDICATE(ast_applicationp, AST_APP)
MAKE_LIST_PREDICATE(ast_definitionp, AST_DEFINITION)
MAKE_LIST_PREDICATE(ast_letp, AST_LET)
MAKE_LIST_PREDICATE(ast_blockp, AST_BLOCK)
MAKE_LIST_PREDICATE(ast_parametersp, AST_PARAMETERS)
MAKE_LIST_PREDICATE(ast_lambdap, AST_LAMBDA)
MAKE_LIST_PREDICATE(ast_clausep, AST_CLAUSE)
MAKE_LIST_PREDICATE(ast_condp, AST_COND)

/* ----------------------------------------------------------------------------- */
/* List operations                                                               */
/* ----------------------------------------------------------------------------- */

// IMPORTANT: start, len, and pos are carried from the AST_NULL
// terminator up through each cons cell.
//
// 'kind' is AST_PARAMETERS or AST_BLOCK (any list).
ast *ast_cons(enum ast_type kind, ast *item, ast *ls) {
  if (!item || !ls) PANIC_NULL();
  ast *e = new_ast(AST_CONS, item->start);
  e->subtype = kind;	
  e->car = item;
  e->cdr = ls;
  return e;
}

ast *ast_car(ast *ls) {
  if (!ls || !ast_consp(ls))
    PANIC("Attempt to access car of %s",
	  ls ? ast_name(ls) : "NULL ARG");
  return ls->car;
}

ast *ast_cdr(ast *ls) {
  if (!ls || !ast_consp(ls))
    PANIC("Attempt to access cdr of %s",
	  ls ? ast_name(ls) : "NULL ARG");
  return ls->cdr;
}

// Call this ONLY on a proper list
void *ast_reduce(astReducer *fn, void *initial_value, ast *ls) {
  void *result = initial_value;
  while (ls && !ast_nullp(ls)) {
    assert(ast_consp(ls));
    result = fn(result, ast_car(ls));
    ls = ast_cdr(ls);
  }
  return result;
}

int ast_length(ast *obj) {
  int len = 0;
  while (obj && ast_consp(obj)) {
    len++;
    obj = obj->cdr;
  }
  return len;
}

// Destructive (in place) reverse
ast *ast_nreverse(ast *ls) {
  // Null arg is an error:
  if (!ls) return NULL;
  // Length 0 (empty list) is already reversed:
  if (ast_nullp(ls)) return ls;
  // Not null and not a cons cell, so cannot reverse it:
  if (!ast_consp(ls)) return NULL;
  // Length 1 list is already reversed:
  if (ast_nullp(ls->cdr)) return ls;
  // Do the usual "reverse a linked list in place" thing:
  ast *curr = ls;
  ast *next = curr->cdr;
  while (!ast_nullp(next)) {
    if (ast_consp(next)) {
      // The usual case: we have a proper list
      ast *nextnext = next->cdr;
      next->cdr = curr;
      curr = next;
      next = nextnext;
    } else {
      // Improper list: dotted pair, error
      ast *tmp = curr->car;
      curr->car = next;
      curr->cdr = tmp;
      return curr;
    }
  }
  ls->cdr = next;
  return curr;
}

// Call this ONLY on a proper list
ast *ast_map(astMapper *fn, ast *ls) {
  if (!fn || !ls)
    PANIC_NULL();
  if (!ast_listp(ls))
    PANIC("map applied to ast that is not a list");
  ast *null = ast_null(0, 0); 
  ast *new = null;
  while (!ast_nullp(ls)) {
    assert(ast_consp(ls));
    new = ast_cons(ls->subtype, fn(ast_car(ls)), new);
    ls = ast_cdr(ls);
  }
  assert(ast_nullp(ls));
  null->subtype = ls->subtype;
  null->start = ls->start;
  return ast_nreverse(new);
}

// Predicate for a proper list: ends in NULL, contains no errors
bool ast_proper_listp(ast *obj) {
  if (!obj) 
    PANIC_NULL();

 tailcall:

  // NULL is a proper list
  if (ast_nullp(obj))
    return true;

  // Presence of any error means we do not have a proper list
  if (ast_errorp(obj))
    return false;

  // If obj is an atom, we do not have a list at all
  if (ast_atomp(obj)) 
    return false;

  // If obj is a cons cell, we must examine its car and cdr
  if (ast_consp(obj)) {
    // Is the car another cons cell?
    if (ast_consp(obj->car)) {
      // Yes, so if the car is a proper list, inspect the cdr
      if (ast_proper_listp(obj->car)) {
	obj = obj->cdr;
	goto tailcall;
      }
      // The car is NOT a proper list
      return false;
    }
    // The car is not a cons cell.  Atoms and NULL are allowed.
    if (ast_atomp(obj->car) || ast_nullp(obj->car)) {
      obj = obj->cdr;
      goto tailcall;
    }
    // Else car of obj is something forbidden
    return false;
  }

  PANIC("Unhandled AST type: %s (%d)", ast_name(obj), obj->type);

}

void free_ast(ast *e) {
  if (!e) return;
 tailcall:
  switch (e->type) {
    case AST_ERROR:
      if (e->error) {
	free(e->error->msg);
	free(e->error);
      }
      break;
    case AST_IDENTIFIER:
    case AST_STRING:
      free(e->str);
      break;
    case AST_INTEGER:
    case AST_NULL:
      break;
    case AST_CONS:
      free_ast(e->car);
      ast *conscell = e;
      e = e->cdr;
      free(conscell);
      goto tailcall;
    default:
      PANIC("reader", "Unhandled AST type %s (%d)", ast_name(e), e->type);
  }
  free(e);
  return;
}

/* ----------------------------------------------------------------------------- */
/* Equality testing                                                              */
/* ----------------------------------------------------------------------------- */

// Shallow comparison
bool ast_node_equal(ast *a, ast *b) {
  if (!a && !b) return true;
  if (!a || !b) return false;
  if (a->type != b->type) return false;
  if (a->subtype != b->subtype) return false;
  // Not checking len or pos
  switch (a->type) {
    case AST_ERROR:
      if (!a->error) PANIC_NULL();
      if (a->error->type != b->error->type) return false;
      if (strncmp(a->error->msg, b->error->msg, MAX_MSGLEN) != 0) return false;
      // Not comparing inputs or input pointers
      return true;
    case AST_IDENTIFIER:
      return (strncmp(a->str, b->str, MAX_IDLEN) == 0);
    case AST_STRING:
      return (strncmp(a->str, b->str, MAX_STRINGLEN) == 0);
    case AST_INTEGER:
      return (a->n == b->n);
    case AST_NULL:
    case AST_CONS:
      return true;
    default:
      PANIC("Unhandled AST type %s (%d)", ast_name(a), a->type);
  }
}

// Deep comparison
bool ast_equal(ast *a, ast *b) {
  while (true) {
    if (!ast_node_equal(a, b)) return false;
    if (!ast_consp(a)) return true;
    if (!ast_equal(a->car, b->car)) return false;
    a = a->cdr;
    b = b->cdr;
  }
}

/* ----------------------------------------------------------------------------- */
/* Copy                                                                          */
/* ----------------------------------------------------------------------------- */

// Shallow copy
ast *ast_node_copy(ast *a) {
  if (!a) return NULL;
  ast_error_details *deets;
  ast *b = new_ast(a->type, a->start);
  b->subtype = a->subtype;
  switch (a->type) {
    case AST_ERROR:
      if (!a->error) PANIC_NULL();
      deets = xmalloc(sizeof(ast_error_details));
      if (!deets) PANIC_OOM();
      b->error = deets;
      deets->type = a->error->type;
      deets->input = a->error->input;
      deets->msg = a->error->msg ? strndup(a->error->msg, MAX_MSGLEN) : NULL;
      return b;
    case AST_IDENTIFIER:
      b->str = strndup(a->str, MAX_IDLEN);
      return b;
    case AST_STRING:
      b->str = strndup(a->str, MAX_STRINGLEN);
      return b;
    case AST_INTEGER:
      b->n = a->n;
      return b;
    case AST_NULL:
	return b;
    case AST_CONS:
      // Set these in case not doing a deep copy 
      b->car = NULL;
      b->cdr = NULL;
      return b;
    default:
      PANIC("Unhandled AST type %s (%d)", ast_name(a), a->type);
  }
}

// Deep copy
ast *ast_copy(ast *a) {
  if (!a) return NULL;
  ast *b = ast_node_copy(a);
  if (!ast_consp(a)) return b;
  b->car = ast_copy(a->car);
  b->cdr = ast_copy(a->cdr);
  return b;
}

/* ----------------------------------------------------------------------------- */
/* Syntax error reporting                                                        */
/* ----------------------------------------------------------------------------- */

// Returns an ast error type that can be passed to error_type_name.
//
// Call this ONLY when ast_errorp(e) is true.  The return value can be
// passed to token_type_name().
//
// To print an error in human-readable form, use print_error().
//
error_type ast_error_type(ast *e) {
  if (e && ast_errorp(e)) return e->error->type;
  PANIC("Expected ast error object, not %s", e ? ast_name(e) : "NULL ARG");
}

static ulen_t line_at_point(ast *a, const char **start) {
  if (!a || !start) PANIC_NULL();
  const char *input = a->error->input;
  const char *point = a->start;
  if (!input || !point) {
    *start = NULL;
    return 0;
  }
  // Edge case: point is EOF
  if (!*point && (point > input)) point--;
  // Edge case: point is newline
  if ((*point == '\n') && (point > input)) point--;
  *start = point;
  while ((*start != input) && (**start != '\n')) (*start)--;
  while (*point && (*point != '\n')) point++;

  return to_ulen(point - *start);
}

void fprint_error(FILE *f, ast *a) {
  if (!a) {
    fprintf(f, "NULL AST argument to print_error()\n");
    return;
  }
  if (a->type != AST_ERROR) {
    fprintf(f, "AST argument to print_error() not an error type (is %s)\n",
	    ast_name(a));
    return;
  }    

  // Always print this info
  fprintf(f, "Syntax error [%s]: %s\n", error_name(a), a->error->msg ?: "");

  // For a lexer panic, there's nothing more we can print
  if (a->error->type == ERR_LEXER) return;

  // Extract input line around a->start (the error position)
  const char *line_start;
  int line_len = line_at_point(a, &line_start);

  // Should we print the line at all?
  if (all_whitespacep(line_start, line_len)) return;

  fprintf(f, "  %.*s\n", line_len, line_start);

  // Maybe print an indicator under the offending part of input
  if ((a->error->type != ERR_STRLEN) &&
      (a->error->type != ERR_IDLEN)) {
    ulen_t offset = to_ulen(a->start - line_start);
    fprintf(f, "  %*s\n", (int) offset + 1, "^");
  }
}

void print_error(ast *a) {
  fprint_error(stdout, a);
}

/* ----------------------------------------------------------------------------- */
/* Printing for debugging (human readable trees)                                 */
/* ----------------------------------------------------------------------------- */

static void fprint_string(FILE *f, char *s) {
  char *printable = escape(s, MAX_STRINGLEN);
  fprintf(f, "%s", printable ?: "STRING ESCAPE FAILED");
  free(printable);
}

static void fprint_expression(FILE *f, ast *exp) {
  switch (exp->type) {
    case AST_TRUE:
    case AST_FALSE:
      fprintf(f, "%s\n", ast_name(exp));
      break;
    case AST_INTEGER:
      fprintf(f, "%" PRId64 "\n", exp->n); 
      break;
    case AST_IDENTIFIER:
      fprintf(f, "%s\n", exp->str);
      break;
    case AST_STRING:
      fprint_string(f, exp->str);
      break;
    case AST_NULL:
      fprintf(f, "NULL");
      switch (exp->subtype) {
	case AST_PARAMETERS:
	case AST_BLOCK: 
	case AST_LAMBDA: 
	case AST_DEFINITION: 
	case AST_LET: 
	case AST_COND:
	  fprintf(f, " %s\n", ast_subtype_name(exp));
	  break;
	default:
	  fprintf(f, "(unspecified)\n"); 
      }
      break;
    case AST_ERROR:
      fprintf(f, "ERROR: %s\n", error_name(exp));
      break;
    default:
      PANIC("Unhandled AST type %s", ast_name(exp));
  }
}

#define MAX_TREE_DEPTH 1024

// Indent and print parent's │ lines as needed.
static void indent(FILE *f, int depth, uint8_t *parents) {
  if (depth > MAX_TREE_DEPTH) {
    depth = MAX_TREE_DEPTH;
    fprintf(f, "MAX TREE DEPTH EXCEEDED ");
  } else {
    for (int i=1; (i < depth); i++) {
      fprintf(f, "%s", (parents[i] ? "│   " : "    "));
    }
  }
}

static void do_printing(FILE *f,
			ast *exp,
			int depth,
			uint8_t *parents,
			bool hasSib) {
  indent(f, depth, parents);

  // If current expression has siblings, print the tee, else the ell
  if (depth > 0) fprintf(f, "%s", (hasSib ? "├── " : "└── "));
  
  // Record whether or not at this depth there are more siblings
  if (depth < MAX_TREE_DEPTH) parents[depth] = hasSib;

  // Print each child.  
  if (ast_consp(exp)) {
    fprintf(f, "%s\n", ast_type_name(exp->subtype));
    while (!ast_nullp(exp)) {
      // The car(exp) has a sibling if cdr(exp) is not null
      do_printing(f, ast_car(exp), depth+1, parents, !ast_nullp(ast_cdr(exp)));
      exp = ast_cdr(exp);
    }
  } else {
    // Not a cons cell.  Print the atom.
    fprint_expression(f, exp);
  }
}

void fprint_ast(FILE *f, ast *exp) {
  // Special case for empty expression
  if (!exp) {
    fprintf(f, "NULL AST\n");
    return;
  }
  uint8_t *parents = xmalloc(MAX_TREE_DEPTH);
  if (!parents) PANIC_OOM();
  memset(parents, 0, MAX_TREE_DEPTH);
  do_printing(f, exp, 0, parents, false);
  free(parents);
}

void print_ast(ast *exp) {
  fprint_ast(stdout, exp);
}

