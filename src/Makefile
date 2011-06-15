
CC = gcc
CFLAGS = -O2
LDFLAGS =
OBJS = tsselect.o

tsselect: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

.c.o:
	$(CC) $(CFLAGS) -c $<

all:tsselect

clean:
	rm $(OBJS) tsselect