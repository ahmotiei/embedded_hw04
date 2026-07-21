CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS := -Iapi
LDLIBS := -lsqlite3 -pthread

TARGET := api/sensor_log_api
MONGOOSE_C := api/mongoose/mongoose.c
MONGOOSE_H := api/mongoose/mongoose.h
SOURCES := api/main.cpp api/sensor_log_store.cpp $(MONGOOSE_C)

TEST_TARGET := tests/test_store
TEST_SOURCES := tests/test_store.cpp api/sensor_log_store.cpp

.PHONY: all clean test-core check-mongoose

all: check-mongoose $(TARGET)

check-mongoose:
	@test -f "$(MONGOOSE_C)" -a -f "$(MONGOOSE_H)" || { \
		echo "Mongoose sources are missing." >&2; \
		echo "Run: ./scripts/fetch_mongoose.sh" >&2; \
		exit 1; \
	}

$(TARGET): $(SOURCES) $(MONGOOSE_H) api/sensor_log_store.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(LDLIBS) -o $@

$(TEST_TARGET): $(TEST_SOURCES) api/sensor_log_store.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_SOURCES) -lsqlite3 -o $@

test-core: $(TEST_TARGET)
	./scripts/create_test_db.sh tests/test_sensor.db
	./$(TEST_TARGET) tests/test_sensor.db

clean:
	rm -f $(TARGET) $(TEST_TARGET) tests/test_sensor.db
