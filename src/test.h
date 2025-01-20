/*  -*- Mode: C; -*-                                                         */
/*                                                                           */
/*  test.h  An exessively simple set of unit test macros                     */
/*                                                                           */
/*  (C) Jamie A. Jennings, 2024                                              */

#ifndef test_h
#define test_h

#include <stdio.h>		// fflush()
#include <stdlib.h>		// exit()
#include <unistd.h>		// isatty()
#include <inttypes.h>

static int _TESTS_PASSED = 0;

/* Tell terminal to undo color settings */
#define TEST__ENDCOLOR "\033[0m"

/* Colors:
   OK = green
   FAIL = red bold
   HEADING = default bold
   INFO = default underline
*/
#define TEST__OKCOLOR "\033[32m"
#define TEST__FAILCOLOR "\033[31;1m"
#define TEST__HEADINGCOLOR "\033[0;1m"
#define TEST__INFOCOLOR "\033[0;4m"

#define TEST__DASHES \
  "----------------------------------------------------------------------"

#define TEST__DECLARE_UNUSED __attribute__((unused))

/* 0=stdin, 1=stdout, 2=stderr */
#define TTY (isatty(1))

/* ----------------------------------------------------------------------------- */

#define TEST_START(argc, argv) do {				\
    if ((argc) <= 0) {						\
      fprintf(stderr, "cannot access program name\n");		\
      exit(1);							\
    }								\
    fflush(NULL);						\
    if (TTY) fputs(TEST__HEADINGCOLOR, stdout);			\
    fprintf(stdout, "$$$ START %s %.*s\n", (argv)[0],		\
	   (int)(70-strlen((argv)[0])), TEST__DASHES);		\
    if (TTY) fputs(TEST__ENDCOLOR, stdout);			\
    _TESTS_PASSED = 0;						\
  } while (0)

#define TEST_END() do {						\
    fflush(NULL);						\
    if (TTY) fputs(TEST__OKCOLOR, stdout);			\
    fprintf(stdout, "\n" "✔ %d tests passed\n", _TESTS_PASSED);	\
    if (TTY) fputs(TEST__HEADINGCOLOR, stdout);			\
    fprintf(stdout, "\n$$$ END %s %.*s\n\n",			\
	    argv[0],						\
	    (int)(72-strlen((argv)[0])),			\
	    TEST__DASHES);					\
    if (TTY) fputs(TEST__ENDCOLOR, stdout);			\
    fflush(stdout);						\
    exit(0);							\
  } while (0)

#define TEST_SECTION(msg) do {				\
    fflush(NULL);					\
    if (TTY) fputs(TEST__HEADINGCOLOR, stdout);		\
    fprintf(stdout, "\n$$$ SECTION %s\n", (msg));	\
    if (TTY) fputs(TEST__ENDCOLOR, stdout);		\
    fflush(NULL);					\
  } while (0)

#define TEST_FAIL_AT_LINE(lineno, ...) do {			\
    fflush(NULL);						\
    if (TTY) fputs(TEST__FAILCOLOR, stdout);			\
    fprintf(stdout, "\n✘ %s:%d: ",				\
	    __FILE__, (lineno));				\
    fprintf(stdout, __VA_ARGS__);				\
    if (TTY) fputs(TEST__ENDCOLOR, stdout);			\
    fputs("\n", stdout);					\
    fflush(NULL);						\
    exit(-1);							\
  } while (0)

#define TEST_FAIL(...) do {			\
    TEST_FAIL_AT_LINE(__LINE__, __VA_ARGS__);	\
  } while (0)

#define TEST_ASSERT(arg) do {			\
    if (!(arg)) {				\
      TEST_FAIL("expected %s", #arg);		\
    }						\
    TEST_RECORD_PASS;				\
  } while (0)

/* 
   E.g.
   TEST_ASSERT_MSG(!55, "did not expect 55");
   TEST_ASSERT_MSG(!55, "did not expect %s", "fifty-five");
*/
#define TEST_ASSERT_MSG(arg, ...) do {		\
    if (!(arg)) {				\
      TEST_FAIL(__VA_ARGS__);			\
    }						\
    TEST_RECORD_PASS;				\
  } while (0)

#define TEST_RECORD_PASS do {				     \
    /* fprintf(stdout, TEST__OKCOLOR "✔" TEST__ENDCOLOR); */ \
    _TESTS_PASSED++;					     \
  } while (0)


#define TEST_ASSERT_AT_LINE(original_line, arg) do {	\
    if (!(arg)) {					\
      TEST_FAIL_AT_LINE((original_line),		\
			"expected " #arg);		\
    }							\
    TEST_RECORD_PASS;					\
  } while (0)

#define TEST_ASSERT_NULL(arg) do {		\
    if ((arg) != NULL) {			\
      TEST_FAIL("expected a NULL value");	\
    }						\
    TEST_RECORD_PASS;				\
  } while (0)

#define TEST_ASSERT_NOT_NULL(arg) do {		\
    if ((arg) == NULL) {			\
      TEST_FAIL("expected a non-NULL value");	\
    }						\
    TEST_RECORD_PASS;				\
  } while (0)

#define TEST_ASSERT_EQ_string(a, b) do {		\
    if (strcmp(a, b) != 0) {				\
      TEST_FAIL("expected equal strings '%s', '%s'",	\
		a, b);					\
    }							\
    TEST_RECORD_PASS;				\
  } while (0)

/* ----------------------------------------------------------------------------- */

#define TEST__MAKE_EQTEST(typename, printcode)				\
  TEST__DECLARE_UNUSED							\
  static void TEST_ASSERT_EQ_##typename(typename a, typename b) {	\
    if (a != b) {							\
      TEST_FAIL("expected equal "					\
		#typename						\
		"values "						\
		printcode ", " printcode,				\
		a, b);							\
    }									\
    TEST_RECORD_PASS;				\
  }

TEST__MAKE_EQTEST(uint32_t, "%u")

/* ----------------------------------------------------------------------------- */

#define TEST_EXPECT_WARNING do {				\
    fflush(NULL);						\
    if (TTY) fputs(TEST__INFOCOLOR, stdout);			\
    fprintf(stdout, "Expect a warning here: [%s:%d]",		\
	    __FILE__, __LINE__);				\
    if (TTY) fputs(TEST__ENDCOLOR, stdout);			\
    fputs("\n", stdout);					\
    fflush(NULL);						\
  } while (0)
    

#endif   // ifndef test_h
