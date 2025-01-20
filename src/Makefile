# -*- Mode: BSDmakefile; -*-                                             
#
# Makefile <csc417>
#

PROGRAM=parse
DESTDIR?=/usr/local

SHELL:=/bin/bash

# We use -fno-sanitize-recover so the program will stop when undefined
# behavior is encountered, instead of simply printing a warning and
# continuing.
ASAN_FLAGS= -fsanitize=address,undefined -fno-sanitize-recover=all

CWARNS = -Wall -Wextra \
	-Wcast-align \
	-Wcast-qual \
	-Wdisabled-optimization \
	-Wpointer-arith \
	-Wshadow \
	-Wsign-compare \
	-Wundef \
	-Wwrite-strings \
	-Wbad-function-cast \
	-Wmissing-prototypes \
	-Wnested-externs \
	-Wstrict-prototypes \
        -Wunreachable-code \
        -Wno-missing-declarations

# Makefile.platform detects the platform (OS, ARCH) and compiler in
# use, so we can adjust settings accordingly

include Makefile.platform

# During development, build with RELEASE_MODE unset or set to 'false'

ifeq ($(RELEASE_MODE),true)
  # Release mode is true (production build)
  COPT=-O2
  ASAN_FLAGS=
else
  # Release mode is false (debugging)
  COPT=-g
  ASAN_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all 
  ifeq ($(COMPILER),gcc) 
    ifeq ($(OS),macos)
      ifeq ($(ARCH),arm64)
        # As of June 2024, gcc does not support -fsanitize=address on macos/arm.  
        ASAN_FLAGS=
      endif
    endif
  endif
endif

CFLAGS= --std=c99 $(COPT) $(ASAN_FLAGS) $(CWARNS)

.PHONY:
all: parsertest parse

# OBJECTS

ast.o: ast.c ast.h parser.c parser.h util.h util.c 
	$(CC) $(CFLAGS) -c -o $@ ast.c

lexer.o: lexer.c lexer.h util.h util.c
	$(CC) $(CFLAGS) -c -o $@ lexer.c

parser.o: parser.c parser.h lexer.c lexer.h util.h util.c
	$(CC) $(CFLAGS) -c -o $@ parser.c

desugar.o: desugar.c desugar.h util.h util.c ast.h ast.c
	$(CC) $(CFLAGS) -c -o $@ desugar.c

# PROGRAMS

parsertest: parsertest.c ast.o desugar.o parser.o lexer.o util.o
	$(CC) $(CFLAGS) -o $@ $< ast.o desugar.o parser.o lexer.o util.o \
	&& cp $@ ..

parse: parse.c ast.o desugar.o parser.o lexer.o util.o
	$(CC) $(CFLAGS) -o $@ $< ast.o desugar.o parser.o lexer.o util.o \
	&& cp $@ ..

# TEST EXECUTION

.PHONY:
test: parsertest $(PROGRAM)
	./parsertest
	./clitest.sh

# UTILITIES

.PHONY:
install: $(PROGRAM)
	@printf "NOTE: DESTDIR is $(DESTDIR)\n"; \
	mkdir -p "$(DESTDIR)/bin"; \
        if [[ $$? -ne 0 ]]; then \
	  echo "Failed to create $(DESTDIR)/bin"; \
	  exit -1;\
	fi; \
	cp "$(PROGRAM)" "$(DESTDIR)/bin/$(PROGRAM)"; \
        if [[ $$? -ne 0 ]]; then \
	  echo "Failed to copy $(PROGRAM)"; \
	  exit -1;\
	fi; \
	printf "NOTE: Copied $(PROGRAM) to $(DESTDIR)/bin\n";

.PHONY:
clean:
	@rm -rf *.o *.dSYM parsertest parse

.PHONY:
tags: *.[ch]
	@if [ -z $(shell which etags) ]; then \
	echo "etags not found, skipping TAGS file update"; \
	else etags -o TAGS *.[ch]; \
	fi

