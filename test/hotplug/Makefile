# Host build of the hot-plug test. Run from this directory: make run
CC     ?= gcc
ROOT   := ../..
CFLAGS := -std=c99 -Wall -Wextra -DSPI_MASTER \
          -I../arq/stubs -I$(ROOT)/PARSECS -I$(ROOT)/ringbuffer -I$(ROOT)/berlib/src/berc

SRCS := test_hotplug.c \
        $(ROOT)/PARSECS/PARSECS_Data.c \
        $(ROOT)/PARSECS/PARSECS_Layer2.c \
        $(ROOT)/PARSECS/PARSECS_Layer3.c \
        $(ROOT)/PARSECS/PARSECS_LowLevelAPI.c \
        $(ROOT)/PARSECS/PARSECS_LowLevelTask.c \
        $(ROOT)/PARSECS/PARSECS_HotPlug.c \
        $(ROOT)/PARSECS/crc16.c \
        $(ROOT)/ringbuffer/ringbuf.c

test_hotplug: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

run: test_hotplug
	./test_hotplug

clean:
	rm -f test_hotplug test_hotplug.exe

.PHONY: run clean
