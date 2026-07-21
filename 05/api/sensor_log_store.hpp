#ifndef SENSOR_LOG_STORE_HPP
#define SENSOR_LOG_STORE_HPP

#include <cstddef>
#include <string>
#include <vector>

struct SensorLogValue {
    std::string recorded_at;
    std::string time;
    std::string value;
};

struct SensorLogQueryResult {
    std::vector<SensorLogValue> values;
    std::vector<std::string> warnings;
    std::size_t databases_checked = 0;
    std::size_t databases_succeeded = 0;
};

class SensorLogStore {
public:
    explicit SensorLogStore(std::vector<std::string> database_paths);

    const std::vector<std::string> &database_paths() const noexcept;

    bool query(
        const std::string &sensor_name,
        const std::string &sensor_id,
        const std::string &date,
        SensorLogQueryResult &result,
        std::string &error
    ) const;

private:
    std::vector<std::string> database_paths_;
};

std::string json_escape(const std::string &text);
std::string build_sensor_log_json(
    const std::string &sensor_name,
    const std::string &sensor_id,
    const std::string &date,
    const SensorLogQueryResult &result,
    bool found
);

#endif
