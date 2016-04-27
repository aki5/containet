
#CFLAGS=-O2 -fomit-frame-pointer -march=sandybridge -mtune=haswell
CFLAGS=-O2 -W -Wall -Ilib -Ilibjson5
#LDFLAGS=-static
.PHONY: all clean test

all: containode containet mocker

test: tests/json_test

mocker: mocker.o lib.a libjson5.a
	$(CC) $(LDFLAGS) -o $@ mocker.o lib.a libjson5.a -lcurl -lssl -lcrypto

containode: containode.o lib.a libjson5.a
	$(CC) $(LDFLAGS) -o $@ containode.o lib.a libjson5.a

containet: containet.o lib.a libjson5.a
	$(CC) $(LDFLAGS) -o $@ containet.o lib.a libjson5.a -lpthread

tests/json_test: tests/json_test.o libjson5.a
	$(CC) $(LDFLAGS) -o $@ tests/json_test.o libjson5.a
	tests/json_test tests/

lib.a: lib/file.o lib/smprintf.o lib/strsplit.o lib/tun.o lib/unsocket.o lib/seccomp.o lib/container.o lib/auth.o
	$(AR) r $@ lib/file.o lib/smprintf.o lib/strsplit.o lib/tun.o lib/unsocket.o lib/seccomp.o lib/container.o lib/auth.o

libjson5.a: libjson5/json.o
	$(AR) r $@ libjson5/json.o

clean:
	rm -f tests/json_test containode containet mocker *.o lib.a libjson5.a lib/*.o libjson5/*.o tests/*.o

%.o: $(wildcard *.h */*.h)
