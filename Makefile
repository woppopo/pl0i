pl0i: pl0i.c
	cc -o pl0i pl0i.c

run: pl0i
	./pl0i run.pl0

clean:
	rm -f ./pl0i

.PHONY: run clean
