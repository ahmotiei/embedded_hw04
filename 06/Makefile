CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDLIBS := -lsqlite3 -pthread

TARGET := daemon/sensor_alert_daemon
SOURCE := daemon/alert_daemon.cpp

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) $(SOURCE) $(LDLIBS) -o $(TARGET)

test: all
	./scripts/test_daemon.sh

clean:
	rm -f $(TARGET) tests/source.db tests/alerts.db
