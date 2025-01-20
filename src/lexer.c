//  -*- Mode: C; -*-                                                       
// 
//  lexer.c
// 
//  (C) Jamie A. Jennings, 2024

#include "lexer.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

size_t token_length(token tok) {
  if (!tok.start) PANIC_NULL();
  return (size_t) tok.len;
}

ulen_t to_ulen(ssize_t len) {
  if (len < 0) PANIC("invalid length (%zd)", len); 
  if (len > ULEN_MAX) PANIC("length (%zd) exceeds ULEN_MAX", len);
  return len;
}

/* ----------------------------------------------------------------------------- */
/* Character predicates, named in lisp style, with a trailing 'p'                */
/* ----------------------------------------------------------------------------- */

#define PREDICATE(name, chr)					\
  static bool name(const char *c) { return (*c == chr); }

PREDICATE(eofp, '\0');
PREDICATE(quotep, '\"');
PREDICATE(commap, ',');
PREDICATE(semicolonp, ';');
PREDICATE(newlinep, '\n');
PREDICATE(returnp, '\r');
PREDICATE(spacep, ' ');
PREDICATE(tabp, '\t');
PREDICATE(openparenp, '(');
PREDICATE(closeparenp, ')');
PREDICATE(openbracep, '{');
PREDICATE(closebracep, '}');
PREDICATE(minusp, '-');
PREDICATE(plusp, '+');
PREDICATE(equalsp, '=');
static bool arrowp(const char *s) { 
  return (*s == '=') && (*s) && (*(s+1) == '>');
}

static bool digitp(const char *c) {
  return ((*c >= '0') && (*c <= '9'));
}

static bool whitespacep(const char *c) {
  return newlinep(c) || spacep(c) || tabp(c) || returnp(c);
}

static bool commentp(const char *c) {
  return (*c++ == '/') && (*c == '/');
}

static bool delimiterp(const char *c) {
  // equals is a prefix of arrow, so we must test for it first
  return whitespacep(c)
    || openparenp(c) || closeparenp(c)
    || openbracep(c) || closebracep(c)
    || commap(c)     || semicolonp(c)
    || arrowp(c)     || equalsp(c)
    || commentp(c)   || eofp(c);
}

// FUTURE: Build these using ESC (from lexer.h)
static const char *string_escape_chars = "\\\"rnt";
static const char *string_escape_values = "\\\"\r\n\t";

// Returns the unescaped value, e.g. \n ==> 10, or -1 on error.
static int unescape_char(const char *sptr) {
  const char *pos = strchr(string_escape_chars, *sptr);
  return (pos && (!eofp(pos))) ? 
    string_escape_values[pos - string_escape_chars] : -1;
}

// This is not a predicate.  It returns the escaped char value, or the
// NUL character to indicate an error.
static char escape_char(const char *c) {
  char *pos = strchr(string_escape_values, *c);
  return (pos && (!eofp(pos))) ?
    string_escape_chars[pos - string_escape_values] : '\0';
}

static const char *scan(bool pred(const char *), const char *s) {
  while (*s && pred(s)) s++;
  return s;
}

static const char *until(bool pred(const char *), const char *s) {
  while (*s && !pred(s)) s++;
  return s;
}

// Unescaping stops:
//   when stop_at(c) is true for current char c ==> success
//   at the end of string ==> failure (expected stop_at())
//   after 'len' bytes ==> failure (expected stop_at())
//
// The escape char is backslash '\'.
// Caller must free the returned string.
//
// The value of *err is always a pointer into 'str'.  *err is:
//
//    c  ==> the first unescaped stop_at() char (when no error)
//   '/' ==> bad escape sequence (error)
//   '\0' ==> read end of string before stop_at() fired (error)
//   NULL ==> read 'len' bytes before stop_at() fired (error)
//   else err points to an illegal character
//
char *unescape(const char *str, size_t len,
	       bool stop_at(const char *c),
	       const char **err) {
  if (!str) return NULL;
  int chr;
  size_t i = 0;
  const char *start = str;
  char *result = xmalloc(len + 1);
  if (!result) PANIC_OOM();
  while (*str && !stop_at(str) && ((str - start) < (ssize_t) len)) {
    // Process escape sequence
    if (*str == ESC) {
      *err = str;		// err points to '\'
      str++;
      if (!*str) {
	free(result);
	*err = str;		// err points to '\0'
	return NULL;
      }
      if ((chr = unescape_char(str)) < 0) {
	// Not a recognized escape char
	free(result);
	return NULL;		// err points to the bad char
      } else {
	result[i++] = chr;
      }
      // Successfully processed escape sequence
      str++;
      continue;
    }
    // Else ordinary char that is not a "stop at" char
    result[i++] = *(str++);
  } // while loop
  // Now set 'err' based on why we stopped processing 'str'
  if (stop_at(str)) {
    *err = str;
    result[i] = '\0';
    return result;
  }
  if (!*str) {
    // End of string found before 'stop_at()' fired
    free(result);
    *err = str;			// err points to '\0'
    return NULL;
  }
  // Else we read too many bytes without 'stop_at()' firing
  free(result);
  *err = NULL;
  return NULL;
}
	
// Caller must free the returned string.
// Processing stops at NUL or after len bytes
char *escape(const char *str, size_t len) {
  if (!str) return NULL;
  int chr, i = 0;
  char *result = xmalloc(2 * len + 3);
  if (!result) PANIC_OOM();
  result[i++] = '"';
  for (size_t k = 0; k < len; k++) {
    if (!*str) break;
    if (!(chr = escape_char(str))) {
      // No escape needed
      result[i++] = *str;
    } else {
      result[i++] = ESC;
      result[i++] = chr;
    }
    str++;
  }
  result[i++] = '"';
  result[i] = '\0';
  return result;
}

bool all_whitespacep(const char *ptr, ulen_t len) {
  const char *end = scan(whitespacep, ptr);
  return (end >= ptr + len);
}


/* ----------------------------------------------------------------------------- */
/* String utilities                                                              */
/* ----------------------------------------------------------------------------- */

// Doing this the old-fashioned slow way.
// See, e.g. https://en.wikipedia.org/wiki/UTF-8
// Returns position of invalid UTF8, or -1 if valid.
static ssize_t invalid_utf8(const char *start, ssize_t len) {
  int cbytes = 0;
  for (ssize_t i = 0; i < len; i++) {
    uint8_t c = start[i];
    if (cbytes) {
      // Check for continuation byte
      if ((c & 0xC0) == 0x80) {
	cbytes--;
	continue;
      } else {
	return i;
      }
    }
    if (!c) return i;		// No NUL bytes in UTF8
    if (c < 0x80) continue;	// ASCII
    if ((c & 0xE0) == 0xC0)	// 2-byte sequence
      cbytes = 1;
    if ((c & 0xF0) == 0xE0)	// 3-byte sequence
      cbytes = 2;
    if ((c & 0xF8) == 0xF0)	// 4-byte sequence
      cbytes = 3;
  }
  return (cbytes == 0) ? -1 : len;
}

// ASCII control chars not allowed, including DEL.
// Returns position of verboten char, or -1 if none found.
static ssize_t contains_verboten(const char *start, ssize_t len) {
  for (ssize_t i = 0; i < len; i++) {
    if (((uint8_t) start[i] < 32) && !whitespacep(&(start[i])))
      return i;
    if (start[i] == 0x7F)
      return i;
  }
  return -1;
}

/* ----------------------------------------------------------------------------- */
/* Creating tokens                                                               */
/* ----------------------------------------------------------------------------- */

#define TOKEN_MAKER(name, toktype)				\
  static token name(const char *tokstart,			\
		    const char *tokend) {			\
    ssize_t len = tokend - tokstart;				\
    if (len < 0) PANIC("invalid token length");			\
    if (len > UINT16_MAX) PANIC("invalid token (too long)");	\
    return (token) {.type = (toktype),				\
		    .start = (tokstart),			\
		    .pos = 0,					\
		    .len = len};				\
  }

TOKEN_MAKER(eof, TOKEN_EOF);
TOKEN_MAKER(comma, TOKEN_COMMA);
TOKEN_MAKER(semicolon, TOKEN_SEMICOLON);
TOKEN_MAKER(equals, TOKEN_EQUALS);
TOKEN_MAKER(arrow, TOKEN_ARROW);
TOKEN_MAKER(openparen, TOKEN_OPENPAREN);
TOKEN_MAKER(closeparen, TOKEN_CLOSEPAREN);
TOKEN_MAKER(openbrace, TOKEN_OPENBRACE);
TOKEN_MAKER(closebrace, TOKEN_CLOSEBRACE);

// Receiving the panic token indicates a bug in our lexer/parser.  It
// means "this should not happen".
token panictoken = (token) {.type = TOKEN_PANIC,
			    .start = NULL,
			    .len = 0,
			    .pos = 0};

// Note: 'end' is allowed to point beyond the end of the
// null-terminated string that begins at 'start'.  The 'strndup'
// function stops at NUL, so we never read past the end of the string.
static token error_token(token_type err,
			 const char *start,
			 const char *end,
			 const char *loc) {
  ulen_t len = to_ulen(end - start);
  ulen_t pos = loc ? to_ulen(loc - start) : 0;
  return (token) {.type = err,
                  .start = start,
                  .len = len,
		  .pos = pos};
}

// Avoiding libc functions like strtol.
//
// INPUT: [+-]? [0-9]+
// RETURNS: true (and retval will be set) on success
//
bool interpret_int(const char *start, ulen_t len, int64_t *retval) {
  if (!retval || !start) PANIC_NULL();

  int sign = 0;
  if (plusp(start)) sign = 1;
  if (minusp(start)) sign = -1;
  if ((sign != 0) && (len < 2)) return false;

  uint32_t i = (sign != 0);
  uint64_t n = *(start + i) - '0';
  uint64_t oldn = n;
  for (++i; i < len; i++) {
    n = 10*n + (*(start + i) - '0');
    // If overflow, report failure
    if (n < oldn) return false;
    oldn = n;
  }
  // If underflow or overflow, report failure
  if ((sign > -1) && (n > INT64_MAX)) return false;
  if ((sign == -1) && (n > (uint64_t) INT64_MAX + 1)) return false;
  *retval = (sign == -1) ? -n : n;
  return true;
}

static token lex_integer(const char **sptr) {
  assert(digitp(*sptr) || minusp(*sptr) || plusp(*sptr));
  const char *start = *sptr;
  *sptr = until(delimiterp, start);
  const char *digits = digitp(start) ? start : start + 1;
  const char *end = scan(digitp, digits);
  ssize_t len = end - start;
  assert(len > 0);
  if (end <= digits)
    // no digits found!
    return error_token(TOKEN_BAD_INTCHAR, start, *sptr, *sptr - 1);
  if (*sptr != end)
    // something other than digits found!
    return error_token(TOKEN_BAD_INTCHAR, start, *sptr, end);
  if (len > MAX_INTLEN)
    // too long!
    return error_token(TOKEN_BAD_INTLEN, start, *sptr, start + MAX_INTLEN);
  
  return (token){.type = TOKEN_INTEGER,
		 .start = start,
		 .len = len};
}

// Return the token type if input matches a keyword, and
// TOKEN_IDENTIFIER if the input does NOT match any keyword.
token_type is_keyword(const char *start, size_t len) {
  for (token_type t = KEYWORD_START; t < KEYWORD_END; t++) {
    const char *keyword = token_type_name(t);
    if ((len == strlen(keyword)) &&
	(strncmp(start, keyword, strlen(keyword)) == 0))
      return t;
  }
  return TOKEN_IDENTIFIER;
}

// Read an identifier or keyword
static token lex_identifier(const char **sptr) {
  const char *start = *sptr;
  *sptr = until(delimiterp, start);
  ssize_t len = *sptr - start;
  // Does identifier exceed max number of bytes?
  if (len > MAX_IDLEN)
    return error_token(TOKEN_BAD_IDLEN, start, *sptr, start + MAX_IDLEN);
  // Is it valid UTF8?
  ssize_t errpos = invalid_utf8(start, len);
  if (errpos != -1) {
    if (errpos == 0)
      return error_token(TOKEN_BAD_CHAR, start, *sptr, start + errpos);
    else
      return error_token(TOKEN_BAD_IDCHAR, start, *sptr, start + errpos);
  }
  // Does it contain any unprintable ASCII chars?
  errpos = contains_verboten(start, len);
  if (errpos != -1) {
    if (errpos == 0)
      return error_token(TOKEN_BAD_CHAR, start, *sptr, start + errpos);
    else
      return error_token(TOKEN_BAD_IDCHAR, start, *sptr, start + errpos);
  }
  // Contents are all good.  Now check for keywords.
  token_type maybe_keyword = is_keyword(start, (size_t) len);
  return (token){.type = maybe_keyword,
		   .start = start,
		   .len = len};      
}

// Find the unescaped quote that marks the end of string
static enum token_type inspect_string(const char **sptr) {
  const char *start = *sptr;
  while (**sptr) {
    if (quotep(*sptr)) break;
    if ((**sptr == ESC) && *(*sptr + 1)) (*sptr)++;
    (*sptr)++;
  }
  // Is the string too long?
  if ((*sptr - start) > MAX_STRINGLEN) return TOKEN_BAD_STRLEN;
  // Did it end with EOF?
  if (**sptr == '\0') return TOKEN_BAD_STREOF;
  // If it ended with a quote, then inspection is done
  if (quotep(*sptr)) {
    (*sptr)++;
    return TOKEN_STRING;
  }
  PANIC("Unhandled condition when reading a string");
}

// NOTE: An unprocessed string contains the delimiting double quotes
// in its first and last bytes.
static token lex_string(const char **sptr) {
  assert(quotep(*sptr));
  const char *start = (*sptr)++;
  token_type status = inspect_string(sptr);
  ssize_t errpos, len = *sptr - start;
  if (status == TOKEN_STRING) {
    // We used to check delimiterp(*sptr) here.  Hmmm...
    errpos = invalid_utf8(start, len);
    if (errpos != -1)
      return error_token(TOKEN_BAD_STRCHAR, start, *sptr, start + errpos);
    return (token){.type = TOKEN_STRING,
                   .start = start,
                   .len = len};
  }
  // Else inspect_string() returned a problem
  return error_token(status, start, *sptr, *sptr);
}

static token lex_whitespace(const char **sptr) {
  const char *start = *sptr;
  *sptr = scan(whitespacep, start);
  ssize_t len = *sptr - start;
  if (len > UINT16_MAX)
    return error_token(TOKEN_BAD_WS, start, *sptr, *sptr);
  return (token){.type = TOKEN_WS,
                 .start = start,
                 .len = len};
}

static token lex_comment(const char **sptr) {
  const char *start = *sptr;
  *sptr = until(newlinep, start);
  ssize_t len = *sptr - start;
  if (len > UINT16_MAX)
    return error_token(TOKEN_BAD_COMMENT, start, *sptr, *sptr);
  return (token){.type = TOKEN_COMMENT,
                 .start = start,
                 .len = len};
}

// Low-level API.  Can be used to peek at comments or whitespace.
token read_token(const char **s) {
  if (!s || !*s) return panictoken;
  const char *start = *s;
  if (openparenp(*s)) return ((*s)++,  openparen(start, *s));
  if (closeparenp(*s)) return ((*s)++, closeparen(start, *s));
  if (openbracep(*s)) return ((*s)++, openbrace(start, *s));
  if (closebracep(*s)) return ((*s)++, closebrace(start, *s));
  if (whitespacep(*s)) return lex_whitespace(s);
  if (digitp(*s) || minusp(*s) || plusp(*s)) return lex_integer(s);
  if (quotep(*s)) return lex_string(s);
  if (commap(*s)) return ((*s)++, comma(start, *s));
  if (semicolonp(*s)) return ((*s)++, semicolon(start, *s));
  if (arrowp(*s)) return ((*s)+=2, arrow(start, *s));
  if (equalsp(*s)) return ((*s)++, equals(start, *s));
  if (commentp(*s)) return lex_comment(s);
  if (eofp(*s)) return eof(start, *s);
  // By making identifier the last possibility, we ensure that ids do
  // not start with delimiters, number chars, a double quote, or the
  // comment start sequence.
  return lex_identifier(s);
}

void print_token(token tok) {
  char *tmp;
  printf("[%s", token_name(tok));
  switch (tok.type) {
    case TOKEN_OPENPAREN:
    case TOKEN_CLOSEPAREN:
    case TOKEN_OPENBRACE:
    case TOKEN_CLOSEBRACE:
    case TOKEN_COMMA:
    case TOKEN_SEMICOLON:
    case TOKEN_DEFINITION:
    case TOKEN_LAMBDA:
    case TOKEN_LAMBDA_ALT:
    case TOKEN_COND:
    case TOKEN_ARROW:
    case TOKEN_EQUALS:
    case TOKEN_EOF:
      break;
    case TOKEN_WS:
    case TOKEN_IDENTIFIER:
    case TOKEN_STRING:
    case TOKEN_COMMENT:
    case TOKEN_INTEGER:
    case TOKEN_BAD_WS:
    case TOKEN_BAD_COMMENT:
    case TOKEN_BAD_IDCHAR:
    case TOKEN_BAD_IDLEN:
    case TOKEN_BAD_STREOF:
    case TOKEN_BAD_STRLEN:
    case TOKEN_BAD_STRCHAR:
    case TOKEN_BAD_STRESC:
    case TOKEN_BAD_CHAR:
    case TOKEN_BAD_INTCHAR:
    case TOKEN_BAD_INTLEN: {
      tmp = escape(tok.start, token_length(tok));
      printf(" %s", tmp);
      free(tmp);
      break;
    }
    case TOKEN_PANIC:		// Indicates a bug somewhere
      break;
    default:
      PANIC("Unhandled token type %s (%d)",
	    token_name(tok), tok.type);
  }
  printf("]\n");
}

