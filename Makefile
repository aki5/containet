
#CFLAGS=-O2 -fomit-frame-pointer -march=sandybridge -mtune=haswell
CFLAGS=-g

HFILES=\
	file.h\
	json.h\
	os.h\
	smprintf.h\
	strsplit.h\
	tun.h\
	unsocket.h\


all: json_test containode containet

containode: containode.o tun.o file.o strsplit.o unsocket.o smprintf.o
	$(CC) -o $@ containode.o tun.o file.o strsplit.o unsocket.o smprintf.o

containet: containet.o unsocket.o json.o
	$(CC) -o $@ containet.o unsocket.o json.o -lpthread

json_test: json_test.o json.o
	$(CC) -o $@ json_test.o json.o

clean:
	rm -f json_test containode containet *.o

%.o: $(HFILES)
