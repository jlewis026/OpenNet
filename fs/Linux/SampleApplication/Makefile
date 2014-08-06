CFLAGS=-c -Wall -D_FILE_OFFSET_BITS=64
LDFLAGS=-L. -ldl -lopennet -lfuse
SOURCES=main.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=OpenNet
G++=g++
all: $(SOURCES) $(EXECUTABLE)
$(EXECUTABLE): $(OBJECTS)
	g++ --std=c++0x -g $(OBJECTS) $(LDFLAGS) -Wl,-rpath . -o $@
.cpp.o:
	g++ -D_FILE_OFFSET_BITS=64 -std=c++0x -g -I../ $(CFLAGS) $< -o $@

