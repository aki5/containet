
#CFLAGS=-O2 -fomit-frame-pointer -march=sandybridge -mtune=haswell
CFLAGS=-O2 -W -Wall -Ilib
#LDFLAGS=-static
.PHONY: all clean test

HFILES=\
	file.h\
	json.h\
	os.h\
	smprintf.h\
	strsplit.h\
	tun.h\
	unsocket.h\

all: json_test containode containet mocker

mocker: mocker.o lib/lib.a
	$(CC) $(LDFLAGS) -o $@ mocker.o lib/lib.a -lcurl -lssl -lcrypto

containode: containode.o lib/lib.a
	$(CC) $(LDFLAGS) -o $@ containode.o lib/lib.a 

containet: containet.o lib/lib.a
	$(CC) $(LDFLAGS) -o $@ containet.o lib/lib.a -lpthread

json_test: json_test.o lib/lib.a
	$(CC) $(LDFLAGS) -o $@ json_test.o lib/lib.a

lib/lib.a: lib/file.o lib/smprintf.o lib/strsplit.o lib/tun.o lib/unsocket.o lib/json.o
	$(AR) r $@ lib/file.o lib/smprintf.o lib/strsplit.o lib/tun.o lib/unsocket.o lib/json.o

clean:
	rm -f json_test containode containet mocker *.o lib/lib.a lib/*.o

test:
	echo no tests at this moment

%.o: $(HFILES)
