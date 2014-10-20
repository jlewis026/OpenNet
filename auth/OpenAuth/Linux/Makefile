CFLAGS=-c -Wall
LDFLAGS=-L. -ldl -lcrypto++
LEGACYSOURCES=sqlite3.c
SOURCES=Platform.cpp OpenNet.cpp
OBJECTS=$(SOURCES:.cpp=.o) $(LEGACYSOURCES:.c=.o)
EXECUTABLE=libOpenAuth.so
G++=g++
all: $(SOURCES) $(EXECUTABLE)
$(EXECUTABLE): $(OBJECTS)
	g++ --std=c++0x --shared -g $(OBJECTS) $(LDFLAGS) -Wl,-rpath . -o $@
.c.o:
	gcc -fPIC -g $(CFLAGS) $< -o $@
.cpp.o:
	g++ -fPIC -std=c++0x -g $(CFLAGS) $< -o $@

