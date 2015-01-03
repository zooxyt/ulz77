CC = gcc
debug:
	$(CC) -Wall -Wextra main.c argsparse.c ulz77.c -o ulz77 -g
prof:
	$(CC) -Wall -Wextra main.c argsparse.c ulz77.c -o ulz77 -O3 -g -pg
release:
	$(CC) -Wall -Wextra main.c argsparse.c ulz77.c -o ulz77 -O3

clean:
	rm -rf ulz77
