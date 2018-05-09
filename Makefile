all:
	gcc server.c -pthread -o server.out
	gcc client.c -o client.out
