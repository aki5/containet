
#CFLAGS=-O2 -fomit-frame-pointer -march=sandybridge -mtune=haswell
CFLAGS=-O2 -W -Wall -Ilib
#LDFLAGS=-static
.PHONY: all clean test

all: containode containet mocker

test: tests/json_test

mocker: mocker.o lib/lib.a
	$(CC) $(LDFLAGS) -o $@ mocker.o lib/lib.a -lcurl -lssl -lcrypto

containode: containode.o lib/lib.a
	$(CC) $(LDFLAGS) -o $@ containode.o lib/lib.a 

containet: containet.o lib/lib.a
	$(CC) $(LDFLAGS) -o $@ containet.o lib/lib.a -lpthread

tests/json_test: tests/json_test.o lib/lib.a
	$(CC) $(LDFLAGS) -o $@ tests/json_test.o lib/lib.a
	tests/json_test tests/

lib/lib.a: lib/file.o lib/smprintf.o lib/strsplit.o lib/tun.o lib/unsocket.o lib/json.o lib/seccomp.o lib/container.o
	$(AR) r $@ lib/file.o lib/smprintf.o lib/strsplit.o lib/tun.o lib/unsocket.o lib/json.o lib/seccomp.o lib/container.o

clean:
	rm -f tests/json_test containode containet mocker *.o lib/lib.a lib/*.o tests/*.o

%.o: $(wildcard *.h */*.h)
