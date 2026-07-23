TARGET := master

CXX := g++
CC := gcc

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -I../mongoose
CFLAGS := -O2 -Wall -Wextra -Wpedantic -I../mongoose
LDLIBS := -lsqlite3 -lmemcached -lpaho-mqtt3c

OBJECTS := \
	main.o \
	cache.o \
	http_client.o \
	sensor_service.o \
	sensor_service_backend.o \
	mqtt_bridge.o \
	mongoose.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDLIBS) -o $@

main.o: main.cpp cache.h http_client.h mqtt_bridge.h sensor_service.h \
	sensor_service_backend.h types.h ../mongoose/mongoose.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

cache.o: cache.cpp cache.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

http_client.o: http_client.cpp http_client.h ../mongoose/mongoose.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

sensor_service.o: sensor_service.cpp sensor_service.h sensor_service_internal.h \
	cache.h types.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

sensor_service_backend.o: sensor_service_backend.cpp sensor_service_backend.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

mqtt_bridge.o: mqtt_bridge.cpp mqtt_bridge.h sensor_service.h types.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

mongoose.o: ../mongoose/mongoose.c ../mongoose/mongoose.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)
