//  -*- Mode: C; -*-                                                        
//                                                                         
//  parsertest.c 
//                                                                         
//  (C) Jamie A. Jennings, 2024.                                              

#define _DEFAULT_SOURCE		// for random, srandom
#include "ast.h"
#include "parser.h"
#include "desugar.h"
#include "util.h"
#include "test.h"

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <locale.h>		// for large number printing

// This controls much of the "ordinary" output
#define PRINTING false

// This controls printing of syntax error messages, for human
// inspection (e.g. to ensure the pointer shows in the right place)
#define PRINT_ERRORS true

// This controls output within loops that iterate many, many times
#define PRINT_ALL false

// This controls whether or not we do a lot of memory- and
// time-consuming tests (we always do a few)
#define FUZZING true

// Input buffer large enough for all of our tests
#define BUFSIZE 10000

#define newline() do { puts(""); } while(0)

static void set(char *dest, const char *src) {
  TEST_ASSERT(strlen(src) <= BUFSIZE);
  strncpy(dest, src, BUFSIZE-1);
  dest[BUFSIZE-1] = '\0';       // strncpy does not always null-terminate dest
}

__attribute__((unused))
static void fill(char *dest, const char c, int n) {
  TEST_ASSERT((n > 0) && (n < BUFSIZE));
  memset(dest, (int) c, n);
  dest[n] = '\0';
}

#define SET(str) do {						\
    set(in, (str));						\
    end = in;							\
    state = (pstate){.input=in, .astart=in, .sptr = &end};	\
  } while (0);

#define FILL(chr, n) do {					\
    fill(in, (chr), (n));					\
    end = in;							\
    state = (pstate){.input=in, .astart=in, .sptr = &end};	\
  } while (0);

typedef enum FuzzMode {
  GOOD_BYTES,
  WITH_BAD_CHAR
} FuzzMode;

// In range 0..max-1
static uint32_t random_in(uint32_t max) {
  return max ? random() % max : 0;
}

static bool whitespace(char c) {
  return (c == 9) || (c == 10) || (c == 13) || (c == 32);
}

static unsigned char random_alpha(void) {
  unsigned char c = random_in(52);
  if (c < 26) return c + 'A';
  return c - 26 + 'a';
}

static unsigned char random_digit(void) {
  unsigned char c = random_in(10);
  return c + '0';
}

// TODO: Add UTF8 char generation
static unsigned char random_id_char(void) {
  // PRINTABLE ASCII: 9..10, 13, 32..126
  // CANNOT EMIT delimeters, whitespace, quote, NUL
  // Shortcut: just choose an ASCII alpha or digit or other
  const char *other = "!@#$%^&*_-+[]'\"<>?/`~|/";
  size_t len = strlen(other);
  unsigned char c = random_in(52 + 10 + len);
  if (c < 26) return c + 'A';
  if (c < 52) return (c - 26) + 'a';
  if (c < 62) return (c - 52) + '0';
  else return other[c - 62];
}

// TODO: Add UTF8 char generation
static unsigned char random_id_start(void) {
  // CANNOT EMIT DELIMTER: { } ( ) , ;
  // WHITESPACE is also a delimiter.
  // CANNOT EMIT DIGIT or + -
  // CANNOT EMIT QUOTE or NUL
  // Shortcut: just choose an ASCII alpha char
  unsigned char c = random_in(52);
  if (c < 26) return c + 'A';
  else return (c - 26) + 'a';
}

static bool printable(char c) {
  return ((c > 32) && (c < 127));
}

// Does not include whitespace
static unsigned char random_printable(void) {
  // c is printable if (c > 32) && (c < 127)
  unsigned char c = random_in(127 - 32 - 1) + 33;
  assert(printable(c));
  return c;
}

static unsigned char random_printable_or_ws(void) {
  // c is printable if (c > 32) && (c < 127)
  // ws is 9 (tab), 10 (newline), 13 (return), and 32 (space)
  const unsigned char ws[4] = "\t\n\r ";
  int n = random_in(127 - 32 - 1) + 33 + 4;
  if (n < 4) return ws[n];
  unsigned char c = n - 4;
  assert(printable(c));
  return c;
}

// Will generate neither whitespace nor NULs
static unsigned char random_unprintable(void) {
  // UNPRINTABLE: 1..8, 11..12, 14..31, 127..127
  unsigned char c;
  const int unprintable_range_size = 8 + 2 + 18 + 1;
  assert(unprintable_range_size == 29);
  int n = random_in(unprintable_range_size) + 1;
  if (n < 9) c = n;             // 1..8 ==> 1..8
  else if (n < 11) c = n + 2;   // 9..10 ==> 11..12
  else if (n < 29) c = n + 3;   // 11..28 ==> 14..31
  else c = 127;
  assert((c != 0) && !whitespace(c));
  assert(!printable(c));
  return c;
}

static void generate_string(char *dest, enum FuzzMode mode) {

  assert(BUFSIZE > MAX_STRINGLEN);
  uint32_t len = random_in(MAX_STRINGLEN-2) + 1;

  if (PRINT_ALL)
    printf("Generating random string with %u bytes\n", len);

  *dest = '"';
  for (uint32_t i = 0; i < len; i++) dest[i+1] = random_printable_or_ws();
  dest[len+1] = '"';
  dest[len+2] = '\0';

  // Fix up: change backslash to '/' and double quote to single quote
  for (uint32_t i = 1; i < len+1; i++) {
    if (dest[i] == '\\') dest[i] = '/';
    if (dest[i] == '"') dest[i] = '\'';
  }

  if (mode == GOOD_BYTES) return;

  // Put some unprintable char somewhere between the start/end quotes
  uint32_t pos = random_in(len) + 1;
  assert((pos > 0) && (pos <= len));
  dest[pos] = random_unprintable();
}

/*
  Return a random char that is
  (1) Printable:
      c is printable if (c > 32) && (c < 127), or if ws.
  (2) But not an allowable id character.  Since we allow
      UTF-8, these are just the unprintable ASCII chars.
  (3) EXCEPT that we don't want to generate parens or
      braces, which are delimiters that, like whitespace,
      will stop the parsing of an identifier
*/
static unsigned char random_id_verboten(void) {
  static char id_verboten[256] = {'\0'};
  static int len = 0;
  // Initialize static variables if needed
  if (id_verboten[0] == '\0') {
    for (int i = 1; i < 127; i++)
      if (!printable(i) && !whitespace(i))
	id_verboten[len++] = i;
    printf("There are %d verboten chars.\n", len);
    if (PRINT_ALL) {
      for (int i = 0; i < len; i++)
	printf("0x%02X ", id_verboten[i]);
      printf("\n");
    }
  }
  return id_verboten[random_in(len)];
}

static uint32_t generate_identifier(char *dest, uint32_t maxlen, enum FuzzMode mode) {
  TEST_ASSERT(maxlen > 0);
  uint32_t len;
  
 tryagain:

  len = random_in(maxlen - 1) + 1;
  if (PRINTING) printf("Generating random idenfitier with %u bytes\n", len);
  dest[0] = random_id_start();
  for (uint32_t i = 1; i < len; i++) {
    dest[i] = random_id_char();
    assert(dest[i] != '\0');
  }
  dest[len] = '\0';

  if (is_keyword(dest, (size_t) len) != TOKEN_IDENTIFIER)
    goto tryagain;

  // Remove any double slashes
  for (uint32_t i = 0; i < len-1; i++) 
    if ((dest[i] == '/') && (dest[i+1] == '/'))
      dest[i] = 'a';

  if (mode != GOOD_BYTES) {
    //printf("*** Generating id with bad byte (len = %u) '%s'\n", len, dest);

    // NOTE: No point inserting delimiters (whitespace, parens,
    // braces, comma, semicolon) into the identifier.  These will stop
    // the parser.  A double slash (comment start) will also stop the
    // parser.
    //
    // We pick a random position at which to insert a character that is
    // not valid for an identifier. 
    uint32_t pos = random_in(len-1) + 1;
    dest[pos] = random_id_verboten();
    //printf("*** (len is %u) Just set dest[%u] = 0x%02X\n", len, pos, dest[pos]);
    //fflush(NULL);
  }

  return len;

}

static uint32_t generate_integer(char *dest, uint32_t maxlen, enum FuzzMode mode) {
  uint32_t len = random_in(maxlen - 1) + 1;
  if (PRINT_ALL) printf("Generating random integer with %u bytes\n", len);
  int roll = random_in(100);
  if (roll < 20) dest[0] = '+';
  else if (roll < 40) dest[0] = '-';
  else dest[0] = random_digit();
  for (uint32_t i = 1; i < len; i++) dest[i] = random_digit();
  dest[len] = '\0';

  if (mode == GOOD_BYTES) return len;

  // NOTE: No point inserting delimiters (whitespace, parens, braces,
  // comma, semicolon).  These will stop the parser.  A double slash
  // (comment start) will also stop the parser.
  //
  // If mode is not GOOD_BYTES, we add make a change below.  We don't
  // want to alter the first char, because we want the parser to start
  // parsing an integer.

  // Change one char, but not the first char
  uint32_t pos = random_in(len) + 1;
  assert((pos > 0) && (pos <= len));
  roll = random_in(100);
  if (roll < 50) {
    dest[pos] = random_unprintable();
  } else {
    dest[pos] = random_alpha();
  }
  return len;
}

static void generate_random_program(char *dest) {
  uint32_t len = random_in(BUFSIZE-1);
  if (PRINT_ALL) printf("Generating random program of %u bytes\n", len);
  for (uint32_t i = 0; i < len; i++) {
    int roll = random_in(100);
    if (roll < 5) dest[i] = '(';
    else if (roll < 10) dest[i] = ')';
    else if (roll < 15) dest[i] = ',';
    else if (roll < 20) dest[i] = '{';
    else if (roll < 25) dest[i] = '}';
    else if (roll < 30) dest[i] = ';';
    else if (roll < 35) dest[i] = " \t\n"[random_in(3)];
    else if (roll < 45) dest[i] = random_digit();
    else if (roll < 96) dest[i] = random_alpha();
    else if (roll < 98) dest[i] = random_printable();
    else {
      if (random_in(50) > 48)
        dest[i] = random_unprintable();
      else
        dest[i] = random_printable();
    }
    // Once in a while, put in a long int or long id
    roll = random_in(1000);
    if (roll == 1) {
      uint32_t amt = 5 * MAX_IDLEN;
      if ((len - i) > amt) {
	amt = generate_identifier(&dest[i], amt, GOOD_BYTES);
	i += amt;
	assert(dest[i] == '\0');
      }
    } else if (roll == 2) {
      uint32_t amt = 3 * MAX_INTLEN;
      if ((len - i) > amt) {
	amt = generate_integer(&dest[i], amt, GOOD_BYTES);
	i += amt;
	assert(dest[i] == '\0');
      }
    }
  }
  dest[len] = '\0';
}

static char *make_list_of_strings(size_t limit, char open, char sep, char close) {
  // New strings go to 'str'
  char tempstr[BUFSIZE];
  char *str = &tempstr[0];
  // Returned list representation is in 'buf'
  char *buf = xmalloc(2 * limit);
  if (!buf) PANIC_OOM();

  size_t len = 0;
  buf[len++] = open;
  while (len <= limit) {
    size_t tmplen;
    generate_string(str, GOOD_BYTES);
    tmplen = strlen(str);
    TEST_ASSERT(tmplen <= MAX_STRINGLEN);
    if ((len + tmplen) > limit) break;
    memcpy(buf+len, str, tmplen);
    len += tmplen;
    buf[len++] = sep;
  }
  // Overwrite the last separator (comma or semicolon)
  buf[len-1] = close;
  buf[len] = '\0';
  if (PRINTING)
    printf("Input len = %zu; (%ld under limit of %zu)\n",
	   len, (ssize_t) ((size_t) limit - len), limit);
  return buf;
}


#define TEST_IDENTIFIER(literal) do {		\
    a = read_ast(&state);			\
    if (PRINTING) print_ast(a);			\
    TEST_ASSERT(a && ast_identifierp(a));	\
    TEST_ASSERT(strcmp(a->str, (literal))==0);	\
  } while(0)

#define TEST_INTEGER(number) do {		\
    a = read_ast(&state);			\
    if (PRINTING) print_ast(a);			\
    TEST_ASSERT(a && ast_integerp(a));		\
    TEST_ASSERT(a->n == (number));		\
  } while(0)

#define TEST_NULL() do {			\
    a = read_ast(&state);			\
    if (PRINTING) print_ast(a);			\
    TEST_NULL(a == NULL);			\
  } while(0)

#define TEST_ERROR() do {			\
    a = read_ast(&state);			\
    if (PRINTING) print_ast(a);			\
    TEST_ASSERT(ast_errorp(a));			\
  } while(0)

__attribute__((unused))
static void print_state(pstate s) {
  int offset = s.astart - s.input;
  int pos = *(s.sptr) - s.input;
  assert(offset > 0);
  printf("Input (%3zu bytes) starts with: %.*s\n", strlen(s.input), 40, s.input);
  printf("AST starts at %3d:             %*s\n", offset, offset+1, "^");
  printf("Current position is at %3d:    %*s\n", pos, pos+1, "|");
}

static ulen_t error_position(ast *a) {
  assert(a && ast_errorp(a));
  if (a->start)
    return to_ulen(a->start - a->error->input);
  return 0;
}

static void test_print_ast_error(ast *a) {
  TEST_EXPECT_WARNING;
  print_error(a);
}

/* ----------------------------------------------------------------------------- */
/* Main                                                                          */
/* ----------------------------------------------------------------------------- */

int main(int argc, char **argv) {

  pstate state;

  char in[BUFSIZE];
  ast *a;

  int64_t n;
  char *tmp;
  const char *end;
  setlocale(LC_ALL, "");	// need for printing numbers
  
  TEST_START(argc, argv);
  printf("Size of Token is %zu bytes\n", sizeof(token));
  printf("Size of AST node is %zu bytes\n", sizeof(ast));
  srandom(time(NULL));

  /* ----------------------------------------------------------------------------- */
  TEST_SECTION("API error handling (should not panic)");

  state = (pstate){.input=NULL, .astart=NULL, .sptr = NULL};
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_LEXER);
  if (PRINTING) test_print_ast_error(a);
  free_ast(a);

  SET("hello");
  state.sptr = NULL;
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_LEXER);
  if (PRINTING) test_print_ast_error(a);
  free_ast(a);

  /* ----------------------------------------------------------------------------- */
  TEST_SECTION("Basic token recognition");

  SET("");
  a = read_ast(&state);
  TEST_ASSERT(a == NULL);

  a = read_ast(&state);
  TEST_ASSERT(a == NULL);

#define EXPECT_ID(input, name) do {			\
    SET(input);						\
    a = read_ast(&state);				\
    TEST_ASSERT(a && ast_identifierp(a));		\
    TEST_ASSERT(strncmp(a->str,				\
			name,				\
			MAX_IDLEN)			\
		== 0);					\
    free_ast(a);					\
  } while (0);  

#define EXPECT_INT(input, number) do {		\
    SET(input);					\
    a = read_ast(&state);			\
    TEST_ASSERT(a && ast_integerp(a));		\
    TEST_ASSERT(a->n == (number));		\
    free_ast(a);				\
  } while (0);  

#define EXPECT_ERROR(input) do {		\
    SET(input);					\
    a = read_ast(&state);			\
    if (PRINTING) {				\
      printf("Received this ast: ");		\
      print_ast(a);				\
      printf("Printing the error report:\n");	\
      test_print_ast_error(a);			\
    }						\
    TEST_ASSERT(a);				\
    TEST_ASSERT(ast_errorp(a));			\
    free_ast(a);				\
  } while (0);  

  // Identifiers: can contain many different characters, including +
  // and -, but cannot start with + or -
  EXPECT_ID("a", "a");
  EXPECT_ID("  a  ", "a");
  EXPECT_ID("    \n\n\t\t  \nX ", "X");
  EXPECT_ID("abc", "abc");
  EXPECT_ID("~", "~");
  EXPECT_ID("az!$&%*/:<>λ?@^_~AZ", "az!$&%*/:<>λ?@^_~AZ");

  // Invalid UTF8 not allowed, e.g. the sequence \xC0 \x0A
  EXPECT_ERROR("az!$&%*/:<>λ?@^_~A\xC0\0AZ");

  // Numbers
  EXPECT_INT("0", 0);
  EXPECT_INT(" 0", 0);
  EXPECT_INT("0\n\n", 0);
  EXPECT_INT(" \n  0 \t ", 0);

  EXPECT_INT("9876543210", 9876543210);
  EXPECT_INT("-1", -1);
  EXPECT_INT(" -1", -1);
  EXPECT_INT(" -1  ", -1);
  EXPECT_INT("+1", 1);
  EXPECT_INT(" +1", 1);
  EXPECT_INT(" +1  ", 1);
  EXPECT_INT("1", 1);
  EXPECT_INT(" 1", 1);
  EXPECT_INT(" 1  ", 1);

  // Number parsing must check for a delimiter
  EXPECT_ERROR("1k");
  EXPECT_ERROR("01.");

  // Check that only the number is read, because a paren delimits the number
  EXPECT_INT("101(abc", 101);
  TEST_ASSERT(end == in + 3);
  EXPECT_INT("202)abc", 202);
  TEST_ASSERT(end == in + 3);

  // Check that only the number is read, because a space delimits the number
  EXPECT_INT("-1 abc", -1);
  TEST_ASSERT(end == in + 2);

#define EXPECT_STR(input, expectation) do {		\
    SET(input);						\
    a = read_ast(&state);				\
    if (a) {						\
      printf("AST type: %s\n", ast_name(a));		\
      fflush(NULL);					\
    }							\
    TEST_ASSERT(a && ast_stringp(a));			\
    printf("String read: '%s'\n", a->str);		\
    fflush(NULL);					\
    TEST_ASSERT(strncmp(a->str,				\
			expectation,			\
			MAX_STRINGLEN)			\
		== 0);					\
    free_ast(a);					\
  } while (0);  

  EXPECT_STR("\"\"", "");	// empty string
  TEST_ASSERT(end == in + strlen(in));

  EXPECT_STR("\n\n\n\"a\"", "a"); // whitespace before the string
  TEST_ASSERT(end == in + strlen(in));

  EXPECT_STR("// Comment\n\"az!$&%*/:<=>?@^_~AZ\"", "az!$&%*/:<=>?@^_~AZ");
  TEST_ASSERT(end == in+strlen(in));

  EXPECT_ERROR("\"\\\"");	// "\"" ==> "

  EXPECT_STR("\"\\\\\"", "\\");	// "\\" ==> "\"  a single backslash inside quotes
  EXPECT_STR("\"\\r\"", "\r");	// "\r" ==> return
  EXPECT_STR("\"\\r\\n\\t\"", "\r\n\t"); // "\r\n\t" ==> return newline tab
  TEST_ASSERT(end == in+strlen(in));

  /* ----------------------------------------------------------------------------- */
  TEST_SECTION("Comments");

#define EXPECT_EOF(input) do {				\
    SET(input);						\
    a = read_ast(&state);				\
    TEST_ASSERT_NULL(a);				\
  } while (0);  

  EXPECT_EOF("//");
  EXPECT_EOF("//       ");
  EXPECT_EOF("//    Hello, world!   ");
  EXPECT_EOF("//\n       ");
  
  // Comment stops at newline
  EXPECT_ID("// Hello, world!\nNext line not part of comment.", "Next");
  TEST_ASSERT(end == in + 21);

  /* ----------------------------------------------------------------------------- */
  TEST_SECTION("Token length and format errors");

#define EXPECT_THIS_ERROR(kind) do {				\
    a = read_ast(&state);					\
    TEST_ASSERT(a);						\
    TEST_ASSERT(ast_errorp(a));					\
    TEST_ASSERT(ast_error_type(a) == (kind));			\
    if (PRINTING) test_print_ast_error(a);			\
    free_ast(a);						\
  } while (0);  

  // ID
  
  // An identifier can be as long as MAX_IDLEN bytes
  FILL('A', MAX_IDLEN);
  TEST_ASSERT(strnlen(in, BUFSIZE) == MAX_IDLEN);
  a = read_ast(&state);
  TEST_ASSERT(a && ast_identifierp(a));
  TEST_ASSERT(strncmp(a->str,
		      in,
		      MAX_IDLEN)
	      == 0);
  free_ast(a);
  TEST_ASSERT(end == in+strlen(in));     

  // One more, and it's an error
  FILL('a', MAX_IDLEN + 1);
  TEST_ASSERT(strnlen(in, BUFSIZE) > MAX_IDLEN);
  EXPECT_THIS_ERROR(ERR_IDLEN);

  SET("+a");
  EXPECT_THIS_ERROR(ERR_INTSYNTAX);
  SET("foo\7b");
  EXPECT_THIS_ERROR(ERR_IDSYNTAX);

  // INT

  FILL('0', MAX_INTLEN);
  TEST_ASSERT(strnlen(in, BUFSIZE) == MAX_INTLEN);
  a = read_ast(&state);
  TEST_ASSERT(a && ast_integerp(a));
  TEST_ASSERT(end == in+strlen(in));     
  TEST_ASSERT(a->n == 0);
  free_ast(a);

  FILL('0', MAX_INTLEN);
  in[0] = '-'; in[1] = '1';
  a = read_ast(&state);
  TEST_ASSERT(a && ast_integerp(a));
  n = -1;
  for (int i = 0; i < (MAX_INTLEN-2); i++) n *= 10;
  TEST_ASSERT(a->n == n);
  free_ast(a);

  // This number is outside the range of an int64_t
  FILL('1', MAX_INTLEN);
  EXPECT_THIS_ERROR(ERR_INTRANGE);
  
  // This number is outside the range of an int64_t
  FILL('9', MAX_INTLEN);
  in[0] = '-';
  EXPECT_THIS_ERROR(ERR_INTRANGE);

  // Not a number
  SET("1a");
  EXPECT_THIS_ERROR(ERR_INTSYNTAX);

  // BYTES

  FILL('a', MAX_STRINGLEN);
  in[0] = '"';
  in[MAX_STRINGLEN-1] = '"';
  TEST_ASSERT(strnlen(in, BUFSIZE) == MAX_STRINGLEN);
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_stringp(a));
  TEST_ASSERT(end == in+strlen(in));     
  free_ast(a);

  FILL('b', MAX_STRINGLEN + 3);
  in[0] = '"';
  in[MAX_STRINGLEN + 2] = '"';
  TEST_ASSERT(strnlen(in, BUFSIZE) == MAX_STRINGLEN + 3);
  a = read_ast(&state);
  TEST_ASSERT(a && ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_STRLEN);
  if (PRINTING) test_print_ast_error(a);
  free_ast(a);

  SET("\"");
  a = read_ast(&state);
  TEST_ASSERT(a && ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_EOF);
  if (PRINTING) test_print_ast_error(a);
  free_ast(a);

  SET("\"\\x\"");
  a = read_ast(&state);
  TEST_ASSERT(a && ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_STRESC);
  if (PRINTING) test_print_ast_error(a);
  free_ast(a);
  
  SET("\"\\xf\"");
  a = read_ast(&state);
  TEST_ASSERT(a && ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_STRESC);
  if (PRINTING) test_print_ast_error(a);
  free_ast(a);

  SET("\"\n\"");
  a = read_ast(&state);
  TEST_ASSERT(a && ast_stringp(a));
  TEST_ASSERT(strnlen(a->str, MAX_STRINGLEN) == 1);
  TEST_ASSERT(*(a->str) == '\n');
  free_ast(a);

  // \a is not a valid escape
  SET("\"\\n\\a\\t\"");
  // Verify that the input is right before we do the real test
  TEST_ASSERT((in[3] == '\\') && (in[4] == 'a')); 
  a = read_ast(&state);
  TEST_ASSERT(a && ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_STRESC);
  if (PRINTING) test_print_ast_error(a);
  free_ast(a);

  /* ----------------------------------------------------------------------------- */

  TEST_SECTION("Printing string tokens");

  const char *actual_string = "Hello!\n\t";
  SET("\"Hello!\\n\\t\"");
  a = read_ast(&state);
  TEST_ASSERT(a && ast_stringp(a));
  TEST_ASSERT(end == in+strlen(in));
  if (PRINTING) {
    printf("Input:      %s\n", in);
    printf("AST_STRING: "); print_ast(a); newline();
  }
  TEST_ASSERT(strncmp(a->str, actual_string, MAX_STRINGLEN) == 0);
  tmp = escape(a->str, MAX_STRINGLEN);
  TEST_ASSERT(tmp);
  TEST_ASSERT(strncmp(tmp, in, MAX_STRINGLEN) == 0);
  free(tmp);
  free_ast(a);
  
  /* ----------------------------------------------------------------------------- */

  TEST_SECTION("Low-level token reader API");

  token tok;
  
  SET("// Hello, world!\nNext line");
  tok = read_token(&end);
  TEST_ASSERT(tok.type == TOKEN_COMMENT);
  TEST_ASSERT(end == in+16);	// Comment stops at newline
  
  tok = read_token(&end);
  TEST_ASSERT(tok.type == TOKEN_WS);
  TEST_ASSERT(end == in+16+1);     // Newline after comment

  tok = read_token(&end);
  TEST_ASSERT(tok.type == TOKEN_IDENTIFIER);
  TEST_ASSERT(end == in+16+1+4);   // "Next" on next line
  TEST_ASSERT(strncmp(tok.start, "Next", token_length(tok)) == 0);

  tok = read_token(&end);
  TEST_ASSERT(tok.type == TOKEN_WS);
  TEST_ASSERT(end == in+16+1+4+1); // Space after "Next"

  tok = read_token(&end);
  TEST_ASSERT(tok.type == TOKEN_IDENTIFIER);
  TEST_ASSERT(end == in+16+1+4+1+4); // "line"
  TEST_ASSERT(strncmp(tok.start, "line", token_length(tok)) == 0);
  
  tok = read_token(&end);
  TEST_ASSERT(tok.type == TOKEN_EOF);
  TEST_ASSERT(end == in+strlen(in));

  SET("{a b c} \"Hi!\" () (1000)(1,2,3)\t\t//comment\n-12345");
  end = in;
  if (PRINTING) {
    printf("About to read this input: \n-----\n%s\n-----\n", end);
    printf("The tokens read are:\n");
  }
  int ct = 28;
  do {
    tok = read_token(&end);
    if (PRINTING) print_token(tok);
    ct--;
  } while (tok.type != TOKEN_EOF);
  if (PRINTING) printf("-----\n");
  TEST_ASSERT(ct == 0);
  
  SET("))");
  tok = read_token(&end);
  TEST_ASSERT(tok.type == TOKEN_CLOSEPAREN);
  TEST_ASSERT(end == in+1);
  tok = read_token(&end);
  TEST_ASSERT(tok.type == TOKEN_CLOSEPAREN);
  TEST_ASSERT(end == in+2);
  tok = read_token(&end);
  TEST_ASSERT(tok.type == TOKEN_EOF);
  TEST_ASSERT(end == in+2);

  // -----------------------------------------------------------------------------
  TEST_SECTION("Parsing errors");

  state = (pstate){.input=NULL, .astart=NULL, .sptr = NULL};
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_LEXER);
  if (PRINT_ERRORS) test_print_ast_error(a);
  TEST_ASSERT(error_position(a) == 0);
  free_ast(a);

  state = (pstate){.input=in, .astart=NULL, .sptr = NULL};
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_LEXER);
  if (PRINT_ERRORS) test_print_ast_error(a);
  TEST_ASSERT(error_position(a) == 0);
  free_ast(a);

  // Bad integer
  SET("+");
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_INTSYNTAX);
  if (PRINT_ERRORS) test_print_ast_error(a);
  TEST_ASSERT(error_position(a) == 0);
  free_ast(a);

  // Bad character DEL = 127 = 0x7F
  SET("\x7F");
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_BADCHAR);
  if (PRINT_ERRORS) test_print_ast_error(a);
  TEST_ASSERT(error_position(a) == 0);
  free_ast(a);

  // The `4 is read correctly as an identifier
  SET("(1, 2, 3, `4)");
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_parametersp(a));
  free_ast(a);

  // Missing separators (commas)
  SET("(1 (2 (3 ` 4)))");
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_PARAMETERS);
  if (PRINT_ERRORS) test_print_ast_error(a);
  TEST_ASSERT(error_position(a) == 3);
  free_ast(a);

  // Bad character in a nested list.  Note the space after the back
  // tick, causing the back tick to be read as an identifier.
  SET("(1, (2, (3, ` 4)))");
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_PARAMETERS);
  if (PRINT_ERRORS) test_print_ast_error(a);
  TEST_ASSERT(error_position(a) == 14);
  free_ast(a);

  // Unfinished parameters
  SET("(\t\t\t\n");
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_EOF);
  if (PRINT_ERRORS) test_print_ast_error(a);
  TEST_ASSERT(error_position(a) == strlen(in));
  free_ast(a);

  // Unfinished parameters
  SET("(1");
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_EOF);
  if (PRINT_ERRORS) test_print_ast_error(a);
  TEST_ASSERT(error_position(a) == strlen(in));
  free_ast(a);

  // Unfinished nested parameters
  SET("(((((1");
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_EOF);
  if (PRINT_ERRORS) test_print_ast_error(a);
  TEST_ASSERT(error_position(a) == strlen(in));
  free_ast(a);
  
  // Unfinished parameters with more than one element
  SET("(1, 2 , 3 ");
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_EOF);
  if (PRINT_ERRORS) test_print_ast_error(a);
  TEST_ASSERT(error_position(a) == strlen(in));
  free_ast(a);

  // Parameters inside another parameters is error
  SET("(1, 2, (3))");
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_PARAMETERS);
  if (PRINT_ERRORS) test_print_ast_error(a);
  TEST_ASSERT(error_position(a) == 7);
  free_ast(a);

  // Application inside parameters is OK
  SET("(1, 2, a())");
  a = read_ast(&state);
  TEST_ASSERT(a);
  if (123) {print_ast(a);}
  TEST_ASSERT(!ast_errorp(a));
  TEST_ASSERT(ast_parametersp(a));
  TEST_ASSERT(ast_listp(a));
  TEST_ASSERT(ast_consp(a));
  TEST_ASSERT(!ast_nullp(a));
  TEST_ASSERT(ast_proper_listp(a));
  free_ast(a);
  
  // Extra close
  SET(")");
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  if (PRINT_ERRORS) test_print_ast_error(a);
  TEST_ASSERT(error_position(a) == 0);
  free_ast(a);
  
  // Extra close after list
  SET("(foo, bar))");
  a = read_ast(&state);
  TEST_ASSERT(a);
  if (123) {print_ast(a);}
  TEST_ASSERT(!ast_nullp(a));
  TEST_ASSERT(ast_listp(a));
  free_ast(a);
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  if (PRINT_ERRORS) test_print_ast_error(a);
  TEST_ASSERT(error_position(a) == 10);
  free_ast(a);

  // Replace the 'X' with bad UTF8 (note error position)
  SET("km!<ggXh");
  in[6] = (signed char) 246;
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_IDSYNTAX);
  if (PRINT_ERRORS) test_print_ast_error(a);
  TEST_ASSERT(error_position(a) == 7);
  free_ast(a);

  // Replace the 'X' with unprintable (note error position)
  SET("km!<ggXh");
  in[6] = (signed char) 7;
  a = read_ast(&state);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_errorp(a));
  TEST_ASSERT(ast_error_type(a) == ERR_IDSYNTAX);
  if (PRINT_ERRORS) test_print_ast_error(a);
  TEST_ASSERT(error_position(a) == 6);
  free_ast(a);

  SET("km!<(X ) h");
  a = read_ast(&state);
  TEST_ASSERT(a);
  if (PRINTING) print_ast(a);
  TEST_ASSERT(ast_applicationp(a));
  free_ast(a);

  // -----------------------------------------------------------------------------
  TEST_SECTION("Applications (function calls)");

#define EXPECT_APP(fname, nargs) do {	\
    a = read_ast(&state);				\
    if (PRINTING) {print_ast(a);}			\
    TEST_ASSERT(a);					\
    TEST_ASSERT(ast_applicationp(a));			\
    TEST_ASSERT(ast_identifierp(ast_car(a)));		\
    TEST_ASSERT(strcmp(ast_car(a)->str, (fname))==0);	\
    TEST_ASSERT(ast_parametersp(ast_cdr(a)));			\
    TEST_ASSERT(ast_length(ast_cdr(a)) == (nargs));		\
    free_ast(a);					\
  } while (0);  

  SET("f()");
  EXPECT_APP("f", 0);

  SET("f(// This is a comment!\n)");
  EXPECT_APP("f", 0);

  SET("   f\n (\n\n)");
  EXPECT_APP("f", 0);
  
  SET("f(1 ) ");
  EXPECT_APP("f", 1);

  SET("f(1, x, y) ");
  EXPECT_APP("f", 3);
  
  SET("f(1, g(x), zebra) ");
  EXPECT_APP("f", 3);

  SET("  <o>(foo(1), bar(1, 2), baz(1, 2, 3), qux()   )");
  EXPECT_APP("<o>", 4);

  // missing the close paren at the end
  EXPECT_ERROR("  <o>(foo(1), bar(1, 2), baz(1, 2, 3), qux() ");

  // missing a comma
  EXPECT_ERROR("  <o>(foo(1), bar(1, 2), baz(1, 2, 3) qux()) ");

  // Valid: A block returns a value which can be a function
  SET("{a}(1)");
  a = read_ast(&state);
  if (PRINTING) print_ast(a);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_applicationp(a));
  TEST_ASSERT(ast_blockp(ast_car(a)));
  TEST_ASSERT(ast_parametersp(ast_cdr(a)));
  TEST_ASSERT(ast_length(ast_cdr(a)) == 1);
  free_ast(a);

  // Valid: (Curried application) A function can return a function
  SET("f(x)()");
  a = read_ast(&state);
  if (1) print_ast(a);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_applicationp(a));
  TEST_ASSERT(ast_applicationp(ast_car(a)));
  TEST_ASSERT(ast_identifierp(ast_car(ast_car(a))));
  TEST_ASSERT(ast_parametersp(ast_cdr(a)));
  TEST_ASSERT(ast_parametersp(ast_cdr(ast_car(a))));
  TEST_ASSERT(ast_length(ast_cdr(ast_car(a))) == 1);
  TEST_ASSERT(ast_length(ast_cdr(a)) == 0);
  free_ast(a);

  SET("f(a)(b, c)(d, e, f)");
  a = read_ast(&state);
  if (1) print_ast(a);
  if (ast_errorp(a)) print_error(a);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_applicationp(a));
  TEST_ASSERT(ast_applicationp(ast_car(a)));
  TEST_ASSERT(ast_applicationp(ast_car(ast_car(a))));
  TEST_ASSERT(ast_identifierp(ast_car(ast_car(ast_car(a)))));
  TEST_ASSERT(ast_parametersp(ast_cdr(a)));
  TEST_ASSERT(ast_length(ast_cdr(a)) == 3);
  free_ast(a);
  
  SET("f(a)(b, c)(d, e, f)");
  a = read_ast(&state);
  if (1) print_ast(a);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_applicationp(a));
  TEST_ASSERT(ast_applicationp(ast_car(a)));
  TEST_ASSERT(ast_parametersp(ast_cdr(a)));
  TEST_ASSERT(ast_length(ast_cdr(a)) == 3);
  free_ast(a);

  SET("{f}({g; h}, j, {k}(1, 2))");
  a = read_ast(&state);
  if (1) print_ast(a);
  TEST_ASSERT(a);
  TEST_ASSERT(ast_applicationp(a));
  TEST_ASSERT(ast_blockp(ast_car(a)));
  TEST_ASSERT(ast_parametersp(ast_cdr(a)));
  TEST_ASSERT(ast_length(ast_cdr(a)) == 3);
  free_ast(a);

  // -----------------------------------------------------------------------------
  TEST_SECTION("Blocks (like 'begin' or 'progn')");

#define EXPECT_BLOCK(input, nexps) do {			\
    SET(input);						\
    a = read_ast(&state);				\
    if (PRINTING) print_ast(a);				\
    TEST_ASSERT(a);					\
    TEST_ASSERT(ast_blockp(a));				\
    TEST_ASSERT(ast_length(a) == (nexps));		\
    free_ast(a);					\
  } while (0);  

  EXPECT_BLOCK("{}", 0);
  EXPECT_ERROR("{");
  EXPECT_ERROR("}");

  EXPECT_BLOCK("//Hi\n   {   \n   \n//More\n }", 0);
  EXPECT_ERROR("//Hi\n   {   \n   \n//More\n ");

  EXPECT_ERROR(" {  1   2    3 } ");
  EXPECT_ERROR(" {  1,  2,    3 } ");
  EXPECT_ERROR(" {  1,  2;    3 } ");
  EXPECT_ERROR(" {  1   2;    3; } ");
  EXPECT_ERROR(" {  1;  2;    3; } ");

  EXPECT_BLOCK(" {  1  } ", 1);

  EXPECT_BLOCK(" {  1; 2; 3 } ", 3);
  EXPECT_BLOCK(" {  w; x; y; z } ", 4);
  EXPECT_BLOCK(" {  w; f(x, y); z } ", 3);

  // -----------------------------------------------------------------------------
  TEST_SECTION("Lambda");

#define EXPECT_LAMBDA(input) do {			\
    SET(input);						\
    a = read_ast(&state);				\
    if (1) print_ast(a);				\
    TEST_ASSERT(a);					\
    if (ast_errorp(a)) print_error(a);			\
    TEST_ASSERT(ast_lambdap(a));			\
    free_ast(a);					\
  } while (0);  

  EXPECT_LAMBDA("lambda(){}");
  EXPECT_LAMBDA("lambda(a){}");
  EXPECT_LAMBDA("lambda(a, b, c, d, e, f){}");
  EXPECT_LAMBDA("lambda(a, b, c, d, e, f){a; b; c; d; e; f; g; h}");
  EXPECT_ERROR("lambda(1){}");
  EXPECT_ERROR("lambda({}){}");
  EXPECT_ERROR("lambda(x, (y)){}");
  EXPECT_ERROR("lambda(x, y, 1, z){}");

  // -----------------------------------------------------------------------------
  TEST_SECTION("List operations");

  ast *r;

#define NREVERSETEST(input, expected) do {			\
    SET(input);							\
    a = read_ast(&state);					\
    TEST_ASSERT(a && !ast_errorp(a));				\
    if (PRINTING) {						\
      printf("%s:\n", ast_name(a));				\
      print_ast(a); newline();					\
    }								\
    a = ast_nreverse(a);					\
    TEST_ASSERT(a && !ast_errorp(a));				\
    if (PRINTING) {						\
      printf("Reversed:\n");					\
      print_ast(a); newline();					\
    }								\
    SET(expected);						\
    r = read_ast(&state);					\
    if (PRINTING) {						\
      printf("Expected:\n");					\
      print_ast(r); newline();					\
    }								\
    TEST_ASSERT(r && !ast_errorp(r));				\
    if (!ast_equal(a, r)) {					\
      printf("Not equal???\n");					\
      print_ast(a);						\
      print_ast(r);						\
    }								\
    TEST_ASSERT(ast_equal(a, r));				\
    free_ast(a);						\
    free_ast(r);						\
  } while (0);

  NREVERSETEST("()", "()");
  NREVERSETEST("(a)", "(a)");
  NREVERSETEST("(a, 1)", "(1, a)");
  NREVERSETEST("(a, {1})", "({1}, a)");
  NREVERSETEST("({a}, 1)", "(1, {a})");
  NREVERSETEST("({{{a}}}, b)", "(b, {{{a}}})");
  NREVERSETEST("({a} , {b})", "({b}    ,   {a})");
  NREVERSETEST("(1, {2; {3}})", "({2; {3}}, 1)");

  NREVERSETEST("{}", "{}");
  NREVERSETEST("{a}", "{a}");
  NREVERSETEST("{a ; 1}", "{1; a}");
  NREVERSETEST("{a ;\n f(1)}", "{f(1); a}");
  NREVERSETEST("{{a}; 1}", "{1; {a}}");
  NREVERSETEST("{{{{{a}}}} ;b}", "{b; {{{{a}}}}}");
  NREVERSETEST("{{a} ; {b}}", "{{b} ;{a}}");
  NREVERSETEST("{1; {2; {3}}}", "{{2; {3}}; 1}");
  
#define DEEPCOPYTEST(input) do {				\
    if (PRINTING) printf("Input: '%s'\n", input);		\
    SET(input);							\
    a = read_ast(&state);					\
    TEST_ASSERT(a && !ast_errorp(a));				\
    if (PRINTING) {						\
      printf("AST: ");						\
      print_ast(a); newline();					\
    }								\
    r = ast_copy(a);						\
    TEST_ASSERT(r && !ast_errorp(r));				\
    if (PRINTING) {						\
      printf("Copy: ");						\
      print_ast(r); newline();					\
    }								\
    TEST_ASSERT(ast_equal(a, r));				\
    free_ast(r);						\
    free_ast(a);						\
  } while (0);

  DEEPCOPYTEST("1234");
  DEEPCOPYTEST("-5432345");
  DEEPCOPYTEST("x");		// short identifier
  DEEPCOPYTEST("identifier");
  DEEPCOPYTEST("\"bytes\"");
  // newlines and tabs render as \n and \t, so we can't include them
  DEEPCOPYTEST("\"some bytes here\"");
  DEEPCOPYTEST("f(x)");
  DEEPCOPYTEST("f(x, y, z)");
  DEEPCOPYTEST("f(g(h(x)))");
  DEEPCOPYTEST("{f(1); f(2); f(3)}");
  DEEPCOPYTEST("{{1}; {{1}; 2}; {{{1}; 2}; 3}}");
  DEEPCOPYTEST("()");
  DEEPCOPYTEST("(a)");
  DEEPCOPYTEST("(a,1)");
  DEEPCOPYTEST("(a,{1})");
  DEEPCOPYTEST("({a} \n\t   , \n\n\n   1)");
  DEEPCOPYTEST("{{{{{a}}}}; b}");
  DEEPCOPYTEST("({a}, {b})");

  // -----------------------------------------------------------------------------
  TEST_SECTION("Fixup of 'let'");

  ast *b;

#define FIXUPTEST(input, output) do {				\
    if (1) printf("Input: '%s'\n", input);		\
    SET(input);							\
    a = read_ast(&state);					\
    TEST_ASSERT(a);						\
    SET(output);							\
    b = read_ast(&state);						\
    TEST_ASSERT(b);						\
    if (1) {						\
      printf("AST: ");						\
      print_ast(a); newline();					\
    }								\
    r = fixup_let(a);						\
    TEST_ASSERT(r);						\
    if (1) {						\
      printf("Fixup: ");					\
      print_ast(r); newline();					\
      printf("Expected: ");					\
      print_ast(b); newline();					\
    }								\
    TEST_ASSERT(ast_equal(r, b));				\
    free_ast(r);						\
    free_ast(b);						\
    free_ast(a);						\
  } while (0);

  FIXUPTEST("let a = 5", "let a = 5 {}");  
  FIXUPTEST("{let a = 5}", "{let a = 5 {}}");  
  FIXUPTEST("{let a = 5; add(a,1)}", "{let a =   5 {add(a,1)}}");  
  FIXUPTEST("{123; let a   = 5; add(a,1)}", "{123; let a =  5 {add(a,1)}}");  
  FIXUPTEST("{123; let a = 5; add(a,1); 456}", "{123; let a =  5 {add(a,1); 456}}");  

  FIXUPTEST("{let a = 5; let b = 10; add(a,b)}",
	    "{let a = 5 {let b = 10 {add(a,b)}}}");  

  FIXUPTEST("{let a = let b = 10}",
	    "{let a = let b = 10 {} {}}");

  TEST_EXPECT_WARNING;
  printf("AST, Fixup, and Expected will all be errors here\n");
  FIXUPTEST("{let a = let b = 10 add(a,b)}",  // Error: improper block
	    "{let a = let b = 10 add(a,b)}"); // Fixup does not alter the error

  FIXUPTEST("let a = 1 {add(a,100)}",   // No change, because the original
	    "let a  = 1 {add(a,100)}"); // exp has a block in it



  // -----------------------------------------------------------------------------
  TEST_SECTION("Parsing lists of random strings");

  // Construct a single parameters: (s_1, s_2, ... s_n) where each s_i is
  // a randomly generated string.  Using the same strings, construct a
  // block: {s_1; s_2; ... s_n}
  size_t limit = FUZZING ? (350 * MAX_STRINGLEN) : (10 * MAX_STRINGLEN);
  int fuzziters = FUZZING ? 150 : 10;

  char *buf;
  for (int i = 1; i <= fuzziters; i++) {

    if ((i % 10) == 0) printf("%4d lists of strings tested\n", i);

    buf = make_list_of_strings(limit, '(', ',', ')');
    end = buf;
    state = (pstate){.input=buf, .astart=buf, .sptr = &end};
    a = read_ast(&state);
    TEST_ASSERT(a);
    TEST_ASSERT(ast_parametersp(a));
    if (PRINTING) printf("Length of parameters is %d\n", ast_length(a));
    free_ast(a);
    free(buf);

    buf = make_list_of_strings(limit, '{', ';', '}');
    end = buf;
    state = (pstate){.input=buf, .astart=buf, .sptr = &end};
    a = read_ast(&state);
    TEST_ASSERT(a);
    TEST_ASSERT(ast_blockp(a));
    if (PRINTING) printf("Length of block is %d\n", ast_length(a));
    free_ast(a);
    free(buf);
  }

  // -----------------------------------------------------------------------------
  TEST_SECTION("Rudimentary fuzzing for strings");

  fuzziters = FUZZING ? 50000 : 1000;

  for (int i = 0; i < fuzziters; i++) {
    generate_string(in, GOOD_BYTES);
    end = in;
    state = (pstate){.input=in, .astart=in, .sptr = &end};
    a = read_ast(&state);
    if (!a) break;		// EOF
    if (ast_errorp(a)) {
      puts("Error in test, expected a string from this input:");
      printf("Input buf: '%s'\n", in);
      TEST_FAIL("Test implementation error");
    }
    TEST_ASSERT(ast_stringp(a));
    free_ast(a);
  }

// With the change to UTF8, we decided to allow any char in a string.

//   fuzziters = FUZZING ? 50000 : 1000;

//   for (int i = 0; i < fuzziters; i++) {
//     generate_string(in, WITH_BAD_CHAR);
//     end = in;
//     a = read_ast(&state);
//     if (!a) break;		// EOF
//     if (ast_errorp(a)) {
//       TEST_ASSERT(ast_error_type(a) == ERR_STRCHAR);
//     } else {
//       printf("Error in test, did not expect this %s expression:\n", ast_name(a));
//       print_ast(a);
//       printf("\nto be generated from this input:\n");
//       printf("Input buf:\n%s\n", in);
//       TEST_FAIL("Test implementation error");
//     }
//     free_ast(a);
//   }

  // -----------------------------------------------------------------------------
  TEST_SECTION("Rudimentary fuzzing for identifiers");

  SET("/'");
  a = read_ast(&state);
  if (PRINTING) {
    printf("Received type %s\n", ast_name(a));
    print_ast(a);
  }
  TEST_ASSERT(ast_identifierp(a));
  free_ast(a);
  
  SET("yS3'");
  a = read_ast(&state);
  if (PRINTING) {
    printf("Received type %s\n", ast_name(a));
    print_ast(a);
    printf("\n\n");
  }
  TEST_ASSERT(ast_identifierp(a));
  free_ast(a);

  fuzziters = FUZZING ? 200000 : 1000;

  for (int i = 1; i <= fuzziters; i++) {
    if ((i % 10000) == 0) printf("%6d identifiers tested\n", i);
    generate_identifier(in, MAX_IDLEN, GOOD_BYTES);
    end = in;
    state = (pstate){.input=in, .astart=in, .sptr = &end};
    a = read_ast(&state);
    if (!a) break;		// EOF
    if (!ast_identifierp(a)) {
      puts("Error in test, expected an identifier from this input:");
      printf("Input buf: '%s'\n", in);
      if (ast_errorp(a))
	print_error(a);
      else
	print_ast(a);
      newline();
      TEST_FAIL("Test implementation error");
    }
    TEST_ASSERT(ast_identifierp(a));
    free_ast(a);
  }

  newline();
  for (int i = 1; i <= fuzziters; i++) {
    if ((i % 10000) == 0) printf("%6d identifiers tested\n", i);
    generate_identifier(in, MAX_IDLEN, WITH_BAD_CHAR);
    end = in;
    state = (pstate){.input=in, .astart=in, .sptr = &end};
    a = read_ast(&state);
    if (!a) break;		// EOF
    if (ast_errorp(a)) {
      TEST_ASSERT(ast_error_type(a) == ERR_IDSYNTAX);
    } else {
      printf("Error in test, did not expect %s:\n", ast_name(a));
      print_ast(a);
      printf("to be generated from this input:\n");
      printf("Input buf: '%s'\n", in);
      TEST_FAIL("Test implementation error");
    }
    free_ast(a);
  }

  // -----------------------------------------------------------------------------
  TEST_SECTION("Rudimentary fuzzing of read_ast()");

  // Looking for segfaults or panics.  If all is ok, this unit test
  // program will finish with a report about what the fuzzer found.

  int total_asts = 0;
  int total_valid = 0;
  int total_errors = 0;
  int total_eof_count = 0;
  int total_bad_chars = 0;
  int total_bad_ids = 0;
  int total_long_ids = 0;
  int total_bad_integers = 0;
  int total_long_integers = 0;
  int total_bad_strings = 0;
  int total_long_strings = 0;
  int total_bad_parameterlists = 0;
  int total_bad_blocks = 0;
  int total_other_errors = 0;
  int total_chars = 0;

  fuzziters = FUZZING ? 50000 : 100;

  for (int i = 1; i <= fuzziters; i++) {
    if ((i % 5000) == 0) printf("%6d strings tested\n", i);
    int total = 0;		// total = valid + error
    int valid_count = 0;
    int error_count = 0;
    int eof_count = 0;
    int bad_char_count = 0;
    int bad_id_count = 0;
    int long_id_count = 0;
    int bad_integer_count = 0;
    int long_integer_count = 0;
    int bad_string_count = 0;
    int long_string_count = 0;
    int bad_parameters_count = 0;
    int bad_block_count = 0;
    int other_error_count = 0;
    generate_random_program(in);
    end = in;
    state = (pstate){.input=in, .astart=in, .sptr = &end};
    total_chars += strlen(in);
    if (PRINT_ALL) printf("Input: '%s'\n", in);
    do {
      a = read_ast(&state);
      if (!a) break;		// EOF
      total++;
      if (!ast_errorp(a)) {
	valid_count++;
        if (PRINT_ALL) {print_ast(a); newline();}
      } else {
        error_count++;
        if (PRINT_ALL) {print_ast(a); newline();}
        switch (ast_error_type(a)) {
	  case ERR_EOF:       eof_count++; break;
	  case ERR_BADCHAR:   bad_char_count++; break;
	  case ERR_IDSYNTAX:  bad_id_count++; break;
	  case ERR_IDLEN:     long_id_count++; break;
	  case ERR_INTSYNTAX: bad_integer_count++; break;
	  case ERR_INTLEN:    long_integer_count++; break;
	  case ERR_STRCHAR:   bad_string_count++; break;
	  case ERR_STRLEN:    long_string_count++; break;
	  case ERR_PARAMETERS:   bad_parameters_count++; break;
	  case ERR_BLOCK:     bad_block_count++; break;
	  default:            other_error_count++;
	}
	// Other errors include e.g. missing comma or semicolon 
      }
      free_ast(a);
    } while (true);

    if (PRINT_ALL)
      printf("From one random input of length %zu: %d ASTs (%d valid, %d errors)\n",
             strlen(in), total, valid_count, error_count);

    total_asts += total;
    total_valid += valid_count;
    total_errors += error_count;
    total_eof_count += eof_count;
    total_bad_chars += bad_char_count;
    total_bad_ids += bad_id_count;
    total_long_ids += long_id_count;
    total_bad_integers += bad_integer_count;
    total_long_integers += long_integer_count;
    total_bad_strings += bad_string_count;
    total_long_strings += long_string_count;
    total_bad_parameterlists += bad_parameters_count;
    total_bad_blocks += bad_block_count;
    total_other_errors += other_error_count;
  }

  printf("\nGenerated and parsed %'d random-ish input strings:\n", fuzziters);
  printf("  Read %'d characters\n", total_chars);
  printf("  Parsed %'10d ASTs\n", total_asts);
  printf("         %'10d of those (%5.2f%%) were valid\n", total_valid,
	 (100 * (float) total_valid / (float) total_asts));
  printf("         %'10d of those (%5.2f%%) were errors:\n", total_errors,
	 (100 * (float) total_errors / (float) total_asts));

  printf("           %'12d (%5.2f%%) EOF encountered\n", total_eof_count,
	 (100 * (float) total_eof_count / (float) total_errors));

  printf("           %'12d (%5.2f%%) bad chars\n", total_bad_chars,
	 (100 * (float) total_bad_chars / (float) total_errors));

  printf("           %'12d (%5.2f%%) bad ids\n", total_bad_ids,
	 (100 * (float) total_bad_ids / (float) total_errors));
  printf("           %'12d (%5.2f%%) ids too long\n", total_long_ids,
	 (100 * (float) total_long_ids / (float) total_errors));

  printf("           %'12d (%5.2f%%) bad integers\n", total_bad_integers,
	 (100 * (float) total_bad_integers / (float) total_errors));
  printf("           %'12d (%5.2f%%) integers too long\n", total_long_integers,
	 (100 * (float) total_long_integers / (float) total_errors));

  printf("           %'12d (%5.2f%%) bad strings\n", total_bad_strings,
	 (100 * (float) total_bad_strings / (float) total_errors));
  printf("           %'12d (%5.2f%%) strings too long\n", total_long_strings,
	 (100 * (float) total_long_strings / (float) total_errors));

  printf("           %'12d (%5.2f%%) bad parameterlists\n", total_bad_parameterlists,
	 (100 * (float) total_bad_parameterlists / (float) total_errors));
  printf("           %'12d (%5.2f%%) bad blocks\n", total_bad_blocks,
	 (100 * (float) total_bad_blocks / (float) total_errors));

  printf("           %'12d (%5.2f%%) other errors\n", total_other_errors,
	 (100 * (float) total_other_errors / (float) total_errors));

  TEST_ASSERT(total_asts == total_valid + total_errors);


  TEST_END();
}
