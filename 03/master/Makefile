TARGET := master

CXX := g++
CC := gcc

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -I../mongoose
CFLAGS := -O2 -Wall -Wextra -I../mongoose
LDLIBS := -lsqlite3 -lmemcached -lpaho-mqtt3c

OBJECTS = main.o cache.o http_client.o sensor_service.o sensor_service_backend.o mqtt_bridge.o mongoose.o

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

sensor_service.o: sensor_service.cpp sensor_service.h
	$(CXX) $(CXXFLAGS) -c sensor_service.cpp -o sensor_service.o

sensor_service_backend.o: sensor_service_backend.cpp sensor_service_backend.h
	$(CXX) $(CXXFLAGS) -c sensor_service_backend.cpp -o sensor_service_backend.o

mqtt_bridge.o: mqtt_bridge.cpp mqtt_bridge.h
	$(CXX) $(CXXFLAGS) -c mqtt_bridge.cpp -o mqtt_bridge.o
	