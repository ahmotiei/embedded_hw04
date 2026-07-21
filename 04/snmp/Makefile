TARGET := sensor_snmp_pass
CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDLIBS := -lsqlite3

.PHONY: all clean

all: $(TARGET)

$(TARGET): sensor_snmp_pass.cpp
	$(CXX) $(CXXFLAGS) sensor_snmp_pass.cpp $(LDLIBS) -o $(TARGET)

clean:
	rm -f $(TARGET)
