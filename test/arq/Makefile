# Host build of the Layer 2/3 ARQ test. Run from this directory: make run
CC     ?= gcc
ROOT   := ../..
CFLAGS := -std=c99 -Wall -Wextra -DSPI_MASTER \
          -Istubs -I$(ROOT)/PARSECS -I$(ROOT)/ringbuffer -I$(ROOT)/berlib/src/berc

SRCS := test_arq.c \
        $(ROOT)/PARSECS/PARSECS_Data.c \
        $(ROOT)/PARSECS/PARSECS_Layer2.c \
        $(ROOT)/PARSECS/PARSECS_Layer3.c \
        $(ROOT)/PARSECS/PARSECS_LowLevelAPI.c \
        $(ROOT)/PARSECS/crc16.c \
        $(ROOT)/ringbuffer/ringbuf.c

test_arq: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

run: test_arq
	./test_arq

clean:
	rm -f test_arq test_arq.exe

.PHONY: run clean
