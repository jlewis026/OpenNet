CFLAGS=-c -Wall
LDFLAGS=-L. -ldl -lcrypto++ -luuid
LEGACYSOURCES=sqlite3.c
SOURCES=StreamFS.cpp
OBJECTS=$(SOURCES:.cpp=.o) $(LEGACYSOURCES:.c=.o)
EXECUTABLE=libopennet.so
G++=g++
all: $(SOURCES) $(EXECUTABLE)
	cp libopennet.so SampleApplication/
	make -C SampleApplication/
$(EXECUTABLE): $(OBJECTS)
	g++ --shared --std=c++0x -g $(OBJECTS) $(LDFLAGS) -Wl,-rpath . -o $@
.c.o:
	gcc -fPIC -g $(CFLAGS) $< -o $@
.cpp.o:
	g++ -fPIC -std=c++0x -g -I../include/ $(CFLAGS) $< -o $@

