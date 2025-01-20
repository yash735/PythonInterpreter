//  -*- Mode: C; -*-                                                        
//
//  parse.c
//
//  (C) Jamie A. Jennings, 2024

#define version "1.2.0"		// Wednesday, November 6, 2024

#include "parser.h"
#include "util.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Maximum size of input (a program to be parsed)
#define BUFMAX (1024 * 10)

typedef enum exitcodes {
  OK,
  ERR_USAGE,			// Bad CLI args
  ERR_SYNTAX,			// Program (input) has syntax error
  ERR_UNPARSED,			// Unparser input remains
  ERR_EMPTY,			// No input provided
  ERR_INTERNAL,			// Should not happen
  ERR_IO,
} exitcodes;

static void help(const char *progname) {
  printf("Usage: %s [options]\n\n", progname);
  printf("  The program (input) is read from stdin.\n\n"
	 "  Options:\n"
	 "    -a    output a json OBJECT always (incl. for numbers, strings)\n"
	 "    -s    output s-expressions instead of json\n"
	 "    -t    output an ASCII tree figure instead of json\n"
         "    -k    list the language keywords (the invalid identifiers)\n"
	 "    -v    print version number\n"
	 "    -h    print this help message\n"
	 "\n"
	 "  Note: As currently configured, true, false, and null are parsed\n"
	 "        as identifiers, not as keywords.  JSON supports true, false,\n"
	 "        and null specially, but that does not mean our parser does.\n\n"
	 "  Examples:\n");
  printf("    %s < prog.txt\n", progname);
  printf("    %s < prog.txt | interp\n", progname);
  printf("    %s -t < prog.txt\n", progname);
  printf("\n");
}

static bool option_tree = false;
static bool option_sexp = false;
static bool option_always_object = false;

static void process_options(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    // Print help and exit
    if (strcmp(argv[i], "-h") == 0) {
      help(argv[0]);
      exit(0);
    }
    // Print keywords and exit
    if (strcmp(argv[i], "-k") == 0) {
      for (token_type t = KEYWORD_START; t < KEYWORD_END; t++)
	printf("%s\n", token_type_name(t));
      exit(0);
    }
    // Print version and exit
    if (strcmp(argv[i], "-v") == 0) {
      printf("%s version %s\n", argv[0], version);
      exit(0);
    }

    if (strcmp(argv[i], "-t") == 0)
      option_tree = true;
    if (strcmp(argv[i], "-s") == 0)
      option_sexp = true;
    if (strcmp(argv[i], "-a") == 0)
      option_always_object = true;
  }
}

static void print_json(ast *exp) {
  char *printable;
  FILE *f = stdout;
  switch (exp->type) {
    case AST_TRUE:
    case AST_FALSE:
      fprintf(f, "%s", ast_name(exp));
      PANIC("Current configuration should not produce this AST type"
	    "  (AST_TRUE or AST_FALSE).  This is a bug!");
      break;
    case AST_INTEGER:
      if (option_always_object)
	fprintf(f, "{\"Number\": %" PRId64 "}", exp->n);
      else
	fprintf(f, "%" PRId64, exp->n); 
      break;
    case AST_IDENTIFIER:
      fprintf(f, "{\"Identifier\": \"%s\"}", exp->str);
      break;
    case AST_STRING:
      printable = escape(exp->str, MAX_STRINGLEN);
      if (option_always_object)
	fprintf(f, "{\"String\": %s}", printable ?: "STRING ESCAPE FAILED");
      else
	fprintf(f, "%s", printable ?: "STRING ESCAPE FAILED");
      free(printable);
      break;
    case AST_CONS:
      fprintf(f, "{\"%s\":[", ast_subtype_name(exp));
      while (!ast_nullp(exp)) {
	print_json(ast_car(exp));
	exp = ast_cdr(exp);
	if (!ast_nullp(exp)) fprintf(f, ",");
      }
      fprintf(f, "]}");
      break;
    case AST_NULL:
      fprintf(f, "{\"%s\":[]}", ast_subtype_name(exp));
      break;
    default:
      PANIC("Unhandled AST type %s", ast_name(exp));
      exit(ERR_INTERNAL);
  }
}

// S-expressions ("symbolic expressions") like Scheme/Lisp
static void print_sexp(ast *exp) {
  char *printable;
  FILE *f = stdout;
  switch (exp->type) {
    case AST_TRUE:
    case AST_FALSE:
      fprintf(f, "%s", ast_name(exp));
      break;
    case AST_INTEGER:
      fprintf(f, "%" PRId64, exp->n); 
      break;
    case AST_IDENTIFIER:
      fprintf(f, "%s", exp->str);
      break;
    case AST_STRING:
      printable = escape(exp->str, MAX_STRINGLEN);
      fprintf(f, "%s", printable ?: "STRING ESCAPE FAILED");
      free(printable);
      break;
    case AST_CONS:
      fprintf(f, "(");
      if ((exp->subtype != AST_CLAUSE) &&
	  (exp->subtype != AST_PARAMETERS) &&
	  (exp->subtype != AST_APP))
	fprintf(f, "%s ", ast_subtype_name(exp));
      while (!ast_nullp(exp)) {
	print_sexp(ast_car(exp));
	exp = ast_cdr(exp);
	if (!ast_nullp(exp)) fprintf(f, " ");
      }
      fprintf(f, ")");
      break;
    case AST_NULL:
      fprintf(f, "(%s)",
	      (exp->subtype != AST_PARAMETERS) ? ast_subtype_name(exp) : "");
      break;
    default:
      PANIC("Unhandled AST type %s", ast_name(exp));
      exit(ERR_INTERNAL);
  }
}

static char *read_input(void) {
  ssize_t len;
  char tinybuf[1];
  char *buf = xmalloc(BUFMAX + 1);
  if (!buf) PANIC_OOM();

  len = read(STDIN_FILENO, buf, BUFMAX);
  if (len == -1) {
    perror("Error reading from stdin");
    exit(ERR_IO);
  }
  if (len == 0) {
    fprintf(stderr, "Empty input\n");
    exit(ERR_IO);
  }
  buf[len] = '\0';

  // Test for more input. If present, then the earlier read() filled
  // the input buffer without reading everything.
  len = read(STDIN_FILENO, tinybuf, 1);
  if (len != 0) {
    fprintf(stderr, "Input too long (max is %d bytes)\n", BUFMAX);
    exit(ERR_IO);
  }
  return buf;
}

/* ----------------------------------------------------------------------------- */
/* Main                                                                          */
/* ----------------------------------------------------------------------------- */

int main(int argc, char **argv) {

  // 'ptr' advances while 'buf' remains the start of the buffer
  const char *ptr;
  char *buf;
  ast *prog;

  process_options(argc, argv);

  buf = read_input();
  ptr = buf;
  prog = read_program(&ptr);

  if (!prog) {
    fprintf(stderr, "Empty input\n");
    exit(ERR_EMPTY);
  }

  if (ast_errorp(prog)) {
    fprint_error(stderr, prog);
    exit(ERR_SYNTAX);
  } 

  if (option_tree)
    print_ast(prog);
  else if (option_sexp)
    print_sexp(prog);
  else
    print_json(prog);

  printf("\n");

  free_ast(prog);
  if (*ptr != '\0') {
    const char *leftover = ptr;
    // Check to see if only whitespace and comments remain
    prog = read_program(&ptr);
    if (prog) {
      // No, we got something semantically interesting
      free_ast(prog);
      fprintf(stderr, "Unparsed input remaining: %s\n", leftover);
      exit(ERR_UNPARSED);
    }
  }

  free(buf);
}
