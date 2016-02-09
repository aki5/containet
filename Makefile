
CFLAGS=-O2 -W -Wall

all: containet

containet: containet.o tun.o file.o strsplit.o
	$(CC) -o containet containet.o tun.o file.o strsplit.o

clean:
	rm -f containet *.o
