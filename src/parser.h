/*  -*- Mode: C; -*-                                                         */
/*                                                                           */
/*  parser.h   Parser for a traditional PL syntax                            */
/*                                                                           */
/*  (C) Jamie A. Jennings, 2024                                              */

#ifndef parser_h
#define parser_h

#define _POSIX_C_SOURCE 200809L
#include "ast.h"

// When TRACING is true, prints each token as it is read
#define TRACING false

// Parser state
typedef struct pstate {
  const char *input;		// the entire input
  const char *astart;		// start of current ast in input
  const char **sptr;		// current position in input
} pstate;

#define in(s) ((s)->input)
#define pos(s) (*((s)->sptr))

// Configurations for read_separated_list
typedef struct rsl_parms {
  bool       (*acceptablep)(ast *);
  ast_type   subtype;
  token_type close;
  token_type sep;
  error_type err;
} rsl_parms;

/* ----------------------------------------------------------------------------- */
/* Program parsing                                                               */
/* ----------------------------------------------------------------------------- */

// Low-level interface
ast  *read_ast(pstate *s);
token read_token(const char **s);

// Primary external interface
ast *read_program(const char **sptr);

#endif

