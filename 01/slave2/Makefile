TARGET := slave2

CXX := g++
CC := gcc

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -I../mongoose
CFLAGS := -O2 -Wall -Wextra -I../mongoose
LDLIBS := -lsqlite3

OBJECTS := main.o mongoose.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDLIBS) -o $@

main.o: main.cpp ../mongoose/mongoose.h
	$(CXX) $(CXXFLAGS) -c main.cpp -o $@

mongoose.o: ../mongoose/mongoose.c ../mongoose/mongoose.h
	$(CC) $(CFLAGS) -c ../mongoose/mongoose.c -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)
