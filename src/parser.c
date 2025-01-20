/*  -*- Mode: C; -*-                                                         */
/*                                                                           */
/*  parser.c   Parser for a traditional PL syntax                            */
/*                                                                           */
/*  (C) Jamie A. Jennings, 2024                                              */

#include "parser.h"
#include "desugar.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* 

  For the grammar and examples of programs, see the README.

  -----------------------------------------------------------------------------
  IMPLEMENTATION
  -----------------------------------------------------------------------------

  The implementation tokenizes the input as it is read by the parser.
  The signature of read_program() is:

    ast *read_program(char **ptr)

  The parser advances 'ptr' as it parses, to facilitate calling
  read_program() repeatedly.

  If 'input' is the string you want to parse, you might write:

    char *ptr = input;
    ast *prog = read_program(&ptr);

  The ast returned will either be an atom or form, as mentioned above,
  or NULL, or an error indicator.

  A return value of NULL indicates EOF.  Error expressions include:
     PROGRAM_INCOMPLETE, signalling a list that is not properly closed
     PROGRAM_EXTRACLOSE, indicating an extraneous closing paren or brace
     PROGRAM_ERROR, where the error is specified by an enclosed token

*/

static bool token_eofp(token tok) {
  return tok.type == TOKEN_EOF;
}
    
static bool atmospherep(token tok) {
  return (tok.type == TOKEN_COMMENT) || (tok.type == TOKEN_WS);
}

/* ----------------------------------------------------------------------------- */
/* Parsing expressions                                                           */
/* ----------------------------------------------------------------------------- */

static token peek_semantic_token(pstate *s) {
  token tok;
  const char *sptr = pos(s);
  do {
    tok = read_token(&sptr);
  } while (atmospherep(tok));
  return tok;
}

static token read_semantic_token(pstate *s) {
  token tok;
  do {
    tok = read_token(s->sptr);
  } while (atmospherep(tok));
  if (TRACING) {
    print_token(tok);
  }
  return tok;
}

static const rsl_parms
rsl_parameters = {ast_formp, AST_PARAMETERS, TOKEN_CLOSEPAREN, TOKEN_COMMA, ERR_PARAMETERS};

static const rsl_parms
rsl_parmlist = {ast_identifierp, AST_PARAMETERS, TOKEN_CLOSEPAREN, TOKEN_COMMA, ERR_PARAMETERS};

static const rsl_parms
rsl_block = {ast_formp, AST_BLOCK, TOKEN_CLOSEBRACE, TOKEN_SEMICOLON, ERR_BLOCK};

static const rsl_parms
rsl_condclause = {ast_formp, AST_CLAUSE, TOKEN_CLOSEPAREN, TOKEN_ARROW, ERR_COND};

// 'Parameters' is a comma-separated list of forms inside parens,
// e.g. (1, 2, f(x)).
//
// A block is a list of expressions, like 'progn' in Lisp or 'begin'
// in Scheme.  It is similar to a brace-delimited block in Rust (which
// can end in an expression) except that here it MUST end in an
// expression.  E.g. {foo; bar(); baz(1, 2, qux(3))}
//
// A cond expression has one or more clauses, each of which has the
// form (test-expression => consequent-expression)
//
static ast *read_separated_list(pstate *s, const rsl_parms p) {
  token tok;
  const char *start = s->astart;
  ast *err, *arg, *ls = ast_null(p.subtype, s->astart);

  // Check for empty list
  tok = peek_semantic_token(s);
  if (tok.type == p.close) {
    read_semantic_token(s);
    return ls;
  }

 tailcall:

  // Read the next form
  arg = read_ast(s);

  // If eof before the closing token, return error
  if (!arg) {
    err = ast_error(ERR_EOF, in(s), pos(s), ast_type_name(p.subtype));
    goto fail;
  }

  // If read_ast() returned an error, pass it along
  if (ast_errorp(arg)) {
    err = arg;
    arg = NULL;
    goto fail;
  }
  
  // Parameters cannot contain another parameters
  if ((p.subtype == AST_PARAMETERS) && ast_parametersp(arg)) {
    err = ast_error(ERR_PARAMETERS, in(s), arg->start,
		     "parameters not allowed here");
    goto fail;
  }
  
  // If we have some acceptable form, then the next token should be
  // either a separator or the closer.
  if (p.acceptablep(arg)) {

    ls = ast_cons(p.subtype, arg, ls);
    ls->start = start;
    arg = NULL;
    tok = read_semantic_token(s);

    if (tok.type == p.sep) {
      // Check for closer, which is not allowed after a separator:
      // parameters cannot end in comma, block must end in expression,
      // not semicolon.
      tok = peek_semantic_token(s);
      if (tok.type != p.close) goto tailcall;

      err = ast_error(ERR_BADCHAR, in(s), pos(s),
		      "spurious separator (or missing item) here");
      goto fail;
    }      

    // Success!
    if (tok.type == p.close)
      return ast_nreverse(ls);

    if (tok.type == TOKEN_EOF) {
      err = ast_error(ERR_EOF, in(s), pos(s), ast_type_name(p.subtype));
      goto fail;
    }

    // List must have separators between elements
    err = ast_error(p.err, in(s), tok.start, "expected separator here");
    goto fail;

  }

  // Got something unacceptable.
  err = ast_error(p.err, in(s), arg->start, "syntax error here");

 fail:
  if (arg) free_ast(arg);
  free_ast(ls);
  return err;
}

static ast *maybe_read_application(ast *exp, pstate *s) {
  token next;
  ast *args, *app = exp;
  
 tailcall:

  // If 'exp' is NOT followed by an open paren, there's no application
  // here, though we may find we have one later, after reading an
  // identifier or a lambda expression.
  next = peek_semantic_token(s);
  if (next.type != TOKEN_OPENPAREN) return app;

  // Otherwise, we have an application
  args = read_ast(s);

  if (!args) {
    free_ast(app);
    return ast_error(ERR_EOF, in(s), pos(s), "truncated input in parameters");
  }

  if (ast_errorp(args)) {
    free_ast(app);
    return args;
  }
  
  if (!ast_parametersp(args))
    PANIC("Unexpected form after open paren: %s (subtype %s)",
	  ast_name(args), ast_type_name(args->subtype));

  app = ast_cons(AST_APP, app, args);
  app->start = app->start;

  // We may have a Curried application, e.g. f(a)(b)
  goto tailcall;
}
  
static ast *check(ast *a,
		  bool (test)(ast *),
		  error_type specific,
		  pstate *s,
		  const char *msg) {

  if (a && ast_errorp(a)) return a;

  if (!a)
    return ast_error(ERR_EOF, in(s), pos(s), msg);

  if (!test(a)) {
    const char *start = a->start;
    free_ast(a);
    return ast_error(specific, in(s), start, msg);
  }

  // Else all is ok
  return a;
}

static ast *read_assignment(ast *id, pstate *s) {
  ast *assignment, *rhs;

  assert(read_semantic_token(s).type == TOKEN_EQUALS);

  rhs = read_ast(s);
  rhs = check(rhs, ast_formp, ERR_ASSIGNMENT, s, "expected expression");
  if (ast_errorp(rhs)) {
    free_ast(id);
    return rhs;
  }

  ast_type t = AST_ASSIGNMENT;
  assignment = ast_cons(t, id, ast_cons(t, rhs, ast_null(t, s->astart)));
  assignment->start = s->astart;
  return assignment;
}

static ast *read_identifier(pstate *s, error_type err) {
  token tok = read_semantic_token(s);
  if (tok.type == TOKEN_IDENTIFIER)
    return ast_identifier(tok);
  // Else signal the error as best we can
  return ast_error(err, in(s), tok.start, "expected identifier");
}

static ast *read_definition(token binder, pstate *s) {
  ast *id, *rhs, *defn, *block;

  id = read_identifier(s, ERR_DEFINITION);

   token tok = read_semantic_token(s);
   if (tok.type != TOKEN_EQUALS)
     return ast_error(ERR_DEFINITION, in(s), pos(s),
 		     "expected equals sign following identifier");

  rhs = read_ast(s);
  rhs = check(rhs, ast_formp, ERR_DEFINITION, s, "expected expression");
  if (ast_errorp(rhs)) {
    free_ast(id);
    return rhs;
  }

  ast_type t;
  switch (binder.type) {
    case TOKEN_LET:        t = AST_LET;        break;
    case TOKEN_DEFINITION: t = AST_DEFINITION; break;
    default:
      PANIC("unhandled binding construct: %s", token_name(binder));
  }

  // If rhs is NOT followed by an open brace, there's no "in" section,
  // e.g. let x 5 {add(x,1)} which means "let x = 5 in { add(x,1) }".
  token next = peek_semantic_token(s);
  if (next.type == TOKEN_OPENBRACE) {
    block = read_ast(s);
    block = check(block, ast_blockp, ERR_DEFINITION, s, "expected code block");
    if (ast_errorp(block)) {
      free_ast(id);
      free_ast(rhs);
      return block;
    } else {
      // No error
      block = ast_cons(AST_BLOCK, block, ast_null(AST_BLOCK, s->astart));
    }
  } else {
    // No optional block present
    block = ast_null(AST_BLOCK, s->astart);
  }
  defn = ast_cons(t, id, ast_cons(t, rhs, block));
  defn->start = s->astart;
  return defn;
}

static ast *read_lambda(pstate *s) {
  ast *lambda, *args, *body;

  token tok = read_semantic_token(s);
  if (tok.type != TOKEN_OPENPAREN) {
    return ast_error(ERR_LAMBDA, in(s), tok.start, "truncated input in lambda (missing parameter list)");
  }
  args = read_separated_list(s, rsl_parmlist);
  args = check(args, ast_parametersp, ERR_LAMBDA, s, "expected parameter list");
  if (ast_errorp(args)) return args;

  tok = read_semantic_token(s);
  if (tok.type != TOKEN_OPENBRACE) {
    return ast_error(ERR_LAMBDA, in(s), tok.start, "missing function body for lambda");
  }
  body = read_separated_list(s, rsl_block);
  body = check(body, ast_blockp, ERR_LAMBDA, s, "invalid function body");

  if (!body)
    return ast_error(ERR_EOF, in(s), pos(s), "truncated input in lambda (missing function body)");
  if (ast_errorp(body))
    return body;

  lambda = ast_cons(AST_LAMBDA, args,
		    ast_cons(AST_BLOCK, body, ast_null(AST_BLOCK, body->start)));
  lambda->start = s->astart;
  return lambda;
}

/*
  cond (f(x) => 1)
       (g(a, b, 3) => {foo; h(a)})
       (true => p(a, b))
*/
static ast *read_cond(pstate *s) {
  token tok;
  ast *clause, *clauses = ast_null(AST_COND, s->astart);

 tailcall:

  tok = read_semantic_token(s);
  if (tok.type != TOKEN_OPENPAREN) {
    free_ast(clauses);
    return ast_error(ERR_COND, in(s), tok.start, "truncated input in cond");
  }

  clause = read_separated_list(s, rsl_condclause);

  clause = check(clause, ast_clausep, ERR_COND, s, "expected cond clause");
  if (ast_errorp(clause)) {
    free_ast(clauses);
    return clause;
  }

  // Ensure length of clause (a list) is two:  (test => consequent)
  if (ast_length(clause) != 2) {
    free_ast(clauses);
    return ast_error(ERR_COND, in(s), clause->start,
		     "improper cond clause: should be (test => consequent)");
  }

  clauses = ast_cons(AST_COND, clause, clauses);
  clauses->start = s->astart;

  // If clause is followed by another open paren, it's another clause
  tok = peek_semantic_token(s);
  if (tok.type == TOKEN_OPENPAREN) goto tailcall;

  return ast_nreverse(clauses);
}

ast *read_ast(pstate *s) {
  ast *exp;
  token tok = read_semantic_token(s);
  if (token_eofp(tok)) return NULL;

 tailcall:
  s->astart = tok.start;
  switch (tok.type) {
    // "Atmosphere"
    case TOKEN_WS:
    case TOKEN_COMMENT:
      tok = read_semantic_token(s);  
      goto tailcall;

    // Start of parameters
    case TOKEN_OPENPAREN:
      return read_separated_list(s, rsl_parameters);

    // Start of block
    case TOKEN_OPENBRACE:
      exp = read_separated_list(s, rsl_block);
      if (ast_errorp(exp)) return exp;
      return maybe_read_application(exp, s);

    // Keywords, identifiers, and literal values
    case TOKEN_LAMBDA:
    case TOKEN_LAMBDA_ALT:
      exp = read_lambda(s);
      if (ast_errorp(exp)) return exp;
      return maybe_read_application(exp, s);
    case TOKEN_COND:
      return read_cond(s);
    case TOKEN_DEFINITION:
    case TOKEN_LET:
      return read_definition(tok, s);
    case TOKEN_IDENTIFIER: 
      if (peek_semantic_token(s).type == TOKEN_EQUALS)
	return read_assignment(ast_identifier(tok), s);
      return maybe_read_application(ast_identifier(tok), s);
    case TOKEN_STRING:
      return ast_string(in(s), tok);
    case TOKEN_INTEGER:
      return ast_integer(in(s), tok);

    // Unexpected syntax
    case TOKEN_EQUALS:
      return ast_error(ERR_BADCHAR, in(s), tok.start, "spurious equals sign");
    case TOKEN_ARROW:
      return ast_error(ERR_BADCHAR, in(s), tok.start, "spurious arrow");
    case TOKEN_CLOSEBRACE:
      return ast_error(ERR_BADCHAR, in(s), tok.start, "spurious closing brace");
    case TOKEN_CLOSEPAREN:
      return ast_error(ERR_BADCHAR, in(s), tok.start, "spurious closing paren");
    case TOKEN_COMMA:
      return ast_error(ERR_BADCHAR, in(s), tok.start, "spurious comma");
    case TOKEN_SEMICOLON:
      return ast_error(ERR_BADCHAR, in(s), tok.start, "spurious semicolon");

    // Encountered end of input before a form was complete
    case TOKEN_EOF:
      return ast_error(ERR_EOF, in(s), tok.start, NULL);
    case TOKEN_BAD_STREOF:
      return ast_error(ERR_EOF, in(s), tok.start, "unterminated string");

    // Encountered a disallowed character (or string escape sequence)
    case TOKEN_BAD_IDCHAR:
      return ast_error_tok(ERR_IDSYNTAX, in(s), tok);
    case TOKEN_BAD_STRCHAR:
      return ast_error_tok(ERR_STRCHAR, in(s), tok);
    case TOKEN_BAD_STRESC:
      return ast_error_tok(ERR_STRESC, in(s), tok);
    case TOKEN_BAD_INTCHAR:
      return ast_error_tok(ERR_INTSYNTAX, in(s), tok);
    case TOKEN_BAD_CHAR:
      return ast_error_tok(ERR_BADCHAR, in(s), tok);

    // Read a value whose representation exceeds configured maximum
    case TOKEN_BAD_IDLEN:
      return ast_error_tok(ERR_IDLEN, in(s), tok);
    case TOKEN_BAD_STRLEN:
      return ast_error_tok(ERR_STRLEN, in(s), tok);
    case TOKEN_BAD_INTLEN:
      return ast_error_tok(ERR_INTLEN, in(s), tok);

    // Unit tests produce this value but ordinarily it should not occur
    case TOKEN_PANIC:
      return ast_error(ERR_LEXER, in(s), NULL, NULL);

    default:
      PANIC("Unhandled token type %s (%d)", token_name(tok), tok.type);
  }
}

/* ----------------------------------------------------------------------------- */
/* External interface                                                            */
/* ----------------------------------------------------------------------------- */


// Read starting at *sptr, and advance it as we go
ast *read_program(const char **sptr) {
  const char *input = *sptr;
  pstate state = {.input=input, .astart=input, .sptr = sptr};
  ast *program = read_ast(&state);
  if (program && ast_listp(program) && (program->subtype == AST_PARAMETERS))
    return ast_error(ERR_PROGRAM, input, input, "This is a parameter list");
  if (program) {
    ast *desugared = fixup_let(program);
    free_ast(program);
    return desugared;
  }
  return NULL;
}
