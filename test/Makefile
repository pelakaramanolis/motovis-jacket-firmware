CC     ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O1 -g
INC    := -I../firmware/components/accel_manager/include
SRC    := ../firmware/components/accel_manager/crash_detect.c test_crash_detect.c

.PHONY: test clean

test: crash_test
	./crash_test

crash_test: $(SRC)
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $@

clean:
	rm -f crash_test
