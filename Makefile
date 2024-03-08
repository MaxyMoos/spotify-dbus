CFLAGS = -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags dbus-1)
LDFLAGS = $(shell pkg-config --libs dbus-1)

SOURCES = src/spotify.c
EXECS = spotify

$(EXECS): $(SOURCES)
	gcc $(CFLAGS)  -o build/$(EXECS) $(SOURCES) $(LDFLAGS)
