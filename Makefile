
CC=clang-7
CFLAGS=-g -fsanitize=address -W -Wall -Ilib -Ilibjson5
LDFLAGS=-fsanitize=address
.PHONY: all clean test

all: containode containet mocker

test: tests/json_test

mocker: mocker.o lib.a libjson5.a
	$(CC) $(LDFLAGS) -o $@ mocker.o lib.a libjson5.a -lcurl

containode: containode.o lib.a libjson5.a
	$(CC) $(LDFLAGS) -o $@ containode.o lib.a libjson5.a

containet: containet.o lib.a libjson5.a
	$(CC) $(LDFLAGS) -o $@ containet.o lib.a libjson5.a -lpthread

tests/json_test: tests/json_test.o libjson5.a
	$(CC) $(LDFLAGS) -o $@ tests/json_test.o libjson5.a
	tests/json_test tests/

lib.a: lib/file.o lib/smprintf.o lib/strsplit.o lib/tun.o lib/unsocket.o lib/seccomp.o lib/container.o lib/auth.o
	$(AR) r $@ lib/file.o lib/smprintf.o lib/strsplit.o lib/tun.o lib/unsocket.o lib/seccomp.o lib/container.o lib/auth.o

libjson5.a: libjson5/json.o libjson5/jsoncheck.o libjson5/jsoncstr.o libjson5/jsonindex.o libjson5/jsonptr.o libjson5/jsonrefs.o libjson5/jsonwalk.o
	$(AR) r $@ $^

clean:
	rm -f tests/json_test containode containet mocker *.o lib.a libjson5.a lib/*.o libjson5/*.o tests/*.o

%.o: $(wildcard *.h */*.h)
