SRCS		:= $(wildcard *.c)
HDRS		:= $(wildcard *.h ../sim.h ../lib/*.h)
OBJS		:= $(SRCS:.c=.o)

CC		:= gcc
CFLAGS		:= -std=c99 -Wall


all: $(OBJS)

$(OBJS): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -c $(SRCS)

clean:
	rm -f $(OBJS)
