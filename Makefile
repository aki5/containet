
CFLAGS=-O2 -W -Wall

all: containode containet

containode: containode.o tun.o file.o strsplit.o unsocket.o
	$(CC) -o containode containode.o tun.o file.o strsplit.o unsocket.o

containet: containet.o unsocket.o
	$(CC) -o containet containet.o unsocket.o -lpthread

clean:
	rm -f containode containet *.o
