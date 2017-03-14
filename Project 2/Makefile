CC=gcc

default: client.c server.c
	$(CC) server.c -o server -lrt
	$(CC) client.c -o client -lrt

clean:
	rm server client received.data
