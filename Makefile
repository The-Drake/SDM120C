CC = gcc
CFLAGS  = -O2 -Wall -g `pkg-config --cflags libmodbus`
LDFLAGS = -O2 -Wall -g `pkg-config --libs libmodbus`

TARGET = sdm120c
OFILES = sdm120c.o RS485_lock.o log.o

all:    ${TARGET}

$(TARGET): $(OFILES)
	$(CC) $(LDFLAGS) -o $@ $(OFILES) 
	chmod 4711 $(TARGET)


%.o: %.c %.h
	$(CC) -c -o $@ $< $(CFLAGS)

strip:
	strip ${TARGET}

clean:
	rm -f *.o ${TARGET}

install: ${TARGET}
	install -m 4711 $(TARGET) /usr/local/bin

uninstall:
	rm -f /usr/local/bin/$(TARGET)
