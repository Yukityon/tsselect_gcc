
CC = gcc
CFLAGS = -O2
LDFLAGS =
OBJS = tsselect.o
PROGRAM = tsselect
DEST = /usr/local/bin

$(PROGRAM): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

.c.o:
	$(CC) $(CFLAGS) -c $<

all:$(PROGRAM)

clean:
	rm $(OBJS) $(PROGRAM)

install:$(PROGRAM)
	install -s $(PROGRAM) $(DEST)