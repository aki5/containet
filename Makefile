
CFLAGS=-O2 -W -Wall

all: containet swtch

containet: containet.o tun.o file.o strsplit.o unsocket.o
	$(CC) -o containet containet.o tun.o file.o strsplit.o unsocket.o

swtch: swtch.o unsocket.o
	$(CC) -o swtch swtch.o unsocket.o -lpthread

clean:
	rm -f containet swtch *.o
