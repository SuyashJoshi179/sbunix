CC = /bin/cc
CFLAGS = -nostdlib -nostdinc -I/include -I/lib/tcc/include

/mnt/hello: hello.c
	$(CC) $(CFLAGS) hello.c /lib/crt.o /lib/libc.a /lib/tcc/libtcc1.a -o /mnt/hello

.PHONY: clean
clean:
	rm -f /mnt/hello
