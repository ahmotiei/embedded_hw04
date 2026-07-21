#include "../api/sensor_log_store.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: test_store <database>\n";
        return 2;
    }

    SensorLogStore store(std::vector<std::string>{argv[1]});
    SensorLogQueryResult result;
    std::string error;

    if (!store.query("temperature", "101", "2026-06-01", result, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    if (result.values.size() != 2 ||
        result.values[0].time != "10:15:00" ||
        result.values[0].value != "24.8" ||
        result.values[1].time != "10:30:00" ||
        result.values[1].value != "25.1") {
        std::cerr << build_sensor_log_json("temperature", "101", "2026-06-01", result, true);
        return 1;
    }

    std::cout << build_sensor_log_json("temperature", "101", "2026-06-01", result, true);
    return 0;
}
