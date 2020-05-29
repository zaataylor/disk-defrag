disk-defrag: disk-defrag.c
	gcc disk-defrag.c -o disk-defrag -g -O0

clean:
	rm -f disk-defrag