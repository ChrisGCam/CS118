CC=gcc

default: client.c server.c
	$(CC) server.c -o server -lrt
	$(CC) client.c -o client -lrt
	$(CC) clientCC.c -o clientCC -lrt
	$(CC) serverCC.c -o serverCC -lrt

clean:
	rm server client received.data
