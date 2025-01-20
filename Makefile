## -*- Mode: Makefile; -*-                                              
##
## CSC 417 Parser
##

TARGET=all
SRCDIR=src
TESTDIR=src

default: release

debug: clean
	@$(MAKE) -C $(SRCDIR) RELEASE_MODE=false $(PROGRAM)

release: clean
	@$(MAKE) -C $(SRCDIR) RELEASE_MODE=true $(PROGRAM)

# ------------------------------------------------------------------

clean:
	-rm -f $(PROGRAM)
	@$(MAKE) -C $(SRCDIR) clean

all install deps tags test config help:
	$(MAKE) -C $(SRCDIR) $@

.PHONY: default debug release clean install deps tags test config help
