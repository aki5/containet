
#CFLAGS=-O2 -fomit-frame-pointer -march=sandybridge -mtune=haswell
CFLAGS=-O2 -W -Wall
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

mocker: mocker.o json.o smprintf.o
	$(CC) $(LDFLAGS) -o $@ mocker.o json.o smprintf.o -lcurl -lssl -lcrypto

containode: containode.o tun.o file.o strsplit.o unsocket.o smprintf.o
	$(CC) $(LDFLAGS) -o $@ containode.o tun.o file.o strsplit.o unsocket.o smprintf.o

containet: containet.o unsocket.o json.o
	$(CC) $(LDFLAGS) -o $@ containet.o unsocket.o json.o -lpthread

json_test: json_test.o json.o
	$(CC) $(LDFLAGS) -o $@ json_test.o json.o

clean:
	rm -f json_test containode containet mocker *.o

test:
	echo no tests at this moment

%.o: $(HFILES)
