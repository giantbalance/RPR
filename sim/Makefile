POL_SRCS	:= $(wildcard policy/*.c)
POL_OBJS	:= $(POL_SRCS:.c=.o)

LIB_SRCS	:= $(wildcard lib/*.c)
LIB_OBJS	:= $(LIB_SRCS:.c=.o)

SRCS		:= $(wildcard *.c)
HDRS		:= $(wildcard *.h, policy/common.h)
OBJS		:= $(SRCS:.c=.o)
TARGET		:= sim

CC		:= gcc
CFLAGS		:= -std=c99 -Wall 

.PHONY: policy lib 

all: policy lib $(TARGET) $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(POL_OBJS) $(LIB_OBJS) -lm

policy:
	$(MAKE) -C policy

lib:
	$(MAKE) -C lib

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -c $(SRCS)

clean: clean_policy clean_lib
	rm -f $(OBJS) $(TARGET)

clean_policy:
	make clean -C policy

clean_lib:
	make clean -C lib
