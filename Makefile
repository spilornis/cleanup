CC ?= cc
CFLAGS ?= -Wall -Wextra -g
CRITERION_PREFIX ?= $(shell brew --prefix criterion 2>/dev/null)
CRITERION_CFLAGS ?= -I$(CRITERION_PREFIX)/include
CRITERION_LDFLAGS ?= -L$(CRITERION_PREFIX)/lib -Wl,-rpath,$(CRITERION_PREFIX)/lib
TEST_LIBS ?= $(CRITERION_LDFLAGS) -lcriterion -lreadline

.PHONY: test

test: tests/cleanup_test.c cleanup.c
	$(CC) $(CFLAGS) $(CRITERION_CFLAGS) -o tests/cleanup_test tests/cleanup_test.c $(TEST_LIBS)
	./tests/cleanup_test
