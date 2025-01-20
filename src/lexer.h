//  -*- Mode: C; -*-                                                       
// 
//  lexer.h
// 
//  (C) Jamie A. Jennings, 2024

#ifndef lexer_h
#define lexer_h

#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>

#define ESC '\\'

#define ULEN_MAX UINT32_MAX
typedef uint32_t ulen_t;

ulen_t to_ulen(ssize_t len);

/* ----------------------------------------------------------------------------- */
/* Tokens                                                                        */
/*   Reader: Input is consumed one token at a time                               */
/*   Writer: The AST_ERROR type contains a token specifying the error            */
/* ----------------------------------------------------------------------------- */

// TOKEN_PANIC indicates a bug in our code, and is not recoverable,
// unlike TOKEN_BAD_*, which indicates bad user input.

#define _TOKENS(X)                                              \
       X(TOKEN_OPENPAREN,      "OPEN_PAREN")		        \
       X(TOKEN_CLOSEPAREN,     "CLOSE_PAREN")		        \
       X(TOKEN_OPENBRACE,      "OPEN_BRACE")		        \
       X(TOKEN_CLOSEBRACE,     "CLOSE_BRACE")			\
       X(TOKEN_COMMA,          "COMMA")                         \
       X(TOKEN_SEMICOLON,      "SEMICOLON")                     \
       X(TOKEN_COMMENT,        "COMMENT")                       \
       X(TOKEN_WS,             "WS")                            \
       X(TOKEN_IDENTIFIER,     "ID")                            \
       X(TOKEN_INTEGER,        "INTEGER")                       \
       X(TOKEN_STRING,         "STRING")		        \
       X(TOKEN_EOF,            "EOF")				\
       /* Keywords: (List must start with LAMBDA.) */		\
       X(TOKEN_LAMBDA,         "lambda")                        \
       X(TOKEN_LAMBDA_ALT,     "λ")                             \
       X(TOKEN_DEFINITION,     "def")                           \
       X(TOKEN_COND,           "cond")                          \
       /* Important that arrow precedes equals (a prefix) */	\
       X(TOKEN_ARROW,          "=>")                            \
       X(TOKEN_EQUALS,         "=")                             \
       X(TOKEN_LET,            "let")                           \
       /* Error types: (List must start with PANIC.) */		\
       X(TOKEN_PANIC,          "PANIC")				\
       X(TOKEN_BAD_WS,         "WHITESPACE_TOO_LONG")		\
       X(TOKEN_BAD_COMMENT,    "COMMENT_TOO_LONG")		\
       X(TOKEN_BAD_IDCHAR,     "INVALID_ID_CHAR")		\
       X(TOKEN_BAD_IDLEN,      "INVALID_ID_LEN")		\
       X(TOKEN_BAD_STREOF,     "UNTERMINATED_STRING")		\
       X(TOKEN_BAD_STRLEN,     "STRING_TOO_LONG")		\
       X(TOKEN_BAD_STRESC,     "INVALID_STRING_ESCAPE")		\
       X(TOKEN_BAD_STRCHAR,    "INVALID_STRING_CHAR")		\
       X(TOKEN_BAD_INTCHAR,    "INVALID_INT_CHAR")		\
       X(TOKEN_BAD_INTLEN,     "INVALID_INT_LEN")		\
       X(TOKEN_BAD_CHAR,       "INVALID_CHAR")			\
       X(TOKEN_NTOKENS,        "SENTINEL")

#define _FIRST(a, b) a,
typedef enum token_type {_TOKENS(_FIRST)} token_type;
#undef _FIRST

#define _SECOND(a, b) b,
static const char *const TOKEN_NAMES[] = {_TOKENS(_SECOND)};
#undef _SECOND

#define token_type_name(typ)					\
  (((0 <= (typ)) &&						\
    (typ) < TOKEN_NTOKENS) ? TOKEN_NAMES[typ] : "INVALID")

#define token_name(tok)						\
  token_type_name(tok.type)

#define KEYWORD_START TOKEN_LAMBDA
#define KEYWORD_END TOKEN_PANIC

typedef struct token {
  enum token_type type;
  ulen_t len;			// length of token in bytes
  ulen_t pos;			// only used for error token
  const char *start;		// start of this token in the input
} token;

bool all_whitespacep(const char *ptr, ulen_t len);

char *escape(const char *str, size_t len);
char *unescape(const char *str, size_t len,
	       bool stop_at(const char *c),
	       const char **err);

bool  interpret_int(const char *start, ulen_t len, int64_t *retval);

token read_token(const char **start);
void  print_token(token tok);

size_t token_length(token tok);

token_type is_keyword(const char *start, size_t len);

#endif
