all: server hunter prey

server:
	gcc server.c -o server

hunter:
	gcc hunter.c -o hunter

prey:
	gcc prey.c -o prey

clean:
	rm -f server hunter prey

