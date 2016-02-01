
CFLAGS=-O2 -W -Wall

all: tainter

tainter: tainter.o tun.o file.o strsplit.o
	$(CC) -o tainter tainter.o tun.o file.o strsplit.o

clean:
	rm -f tainter *.o
