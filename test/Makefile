CC     ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O1 -g

CRASH_INC := -I../firmware/components/accel_manager/include
CRASH_SRC := ../firmware/components/accel_manager/crash_detect.c test_crash_detect.c

NMEA_INC  := -I../firmware/components/gnss_manager/include
NMEA_SRC  := ../firmware/components/gnss_manager/nmea.c test_nmea.c

.PHONY: test clean

test: crash_test nmea_test
	./crash_test
	./nmea_test

crash_test: $(CRASH_SRC)
	$(CC) $(CFLAGS) $(CRASH_INC) $(CRASH_SRC) -o $@

nmea_test: $(NMEA_SRC)
	$(CC) $(CFLAGS) $(NMEA_INC) $(NMEA_SRC) -o $@

clean:
	rm -f crash_test nmea_test
