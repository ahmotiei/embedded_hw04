TARGET := master

CXX := g++
CC := gcc

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -I../mongoose
CFLAGS := -O2 -Wall -Wextra -I../mongoose
LDLIBS := -lsqlite3

OBJECTS := main.o http_client.o mongoose.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDLIBS) -o $@

main.o: main.cpp http_client.h ../mongoose/mongoose.h
	$(CXX) $(CXXFLAGS) -c main.cpp -o $@

http_client.o: http_client.cpp http_client.h ../mongoose/mongoose.h
	$(CXX) $(CXXFLAGS) -c http_client.cpp -o $@

mongoose.o: ../mongoose/mongoose.c ../mongoose/mongoose.h
	$(CC) $(CFLAGS) -c ../mongoose/mongoose.c -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)
