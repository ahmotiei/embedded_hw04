#include "sensor_log_store.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>

namespace {

std::string sqlite_text(sqlite3_stmt *statement, int column) {
    const unsigned char *value = sqlite3_column_text(statement, column);
    return value == nullptr ? "" : reinterpret_cast<const char *>(value);
}

std::string time_part(const std::string &recorded_at) {
    const std::size_t separator = recorded_at.find_first_of(" T");
    if (separator == std::string::npos || separator + 1 >= recorded_at.size()) {
        return recorded_at;
    }

    std::string time = recorded_at.substr(separator + 1);
    const std::size_t timezone = time.find_first_of("Z+-", 1);
    if (timezone != std::string::npos) {
        time.erase(timezone);
    }
    if (time.size() > 8) {
        time.resize(8);
    }
    return time;
}

bool query_one_database(
    const std::string &database_path,
    const std::string &sensor_name,
    const std::string &sensor_id,
    const std::string &date,
    std::vector<SensorLogValue> &values,
    std::string &error
) {
    sqlite3 *database = nullptr;
    const int open_result = sqlite3_open_v2(
        database_path.c_str(),
        &database,
        SQLITE_OPEN_READONLY,
        nullptr
    );

    if (open_result != SQLITE_OK) {
        error = database == nullptr ? "could not open SQLite database" : sqlite3_errmsg(database);
        if (database != nullptr) {
            sqlite3_close(database);
        }
        return false;
    }

    sqlite3_busy_timeout(database, 3000);

    const char *query = R"SQL(
        SELECT
            r.recorded_at,
            CAST(r.value AS TEXT)
        FROM sensor_readings AS r
        JOIN sensors AS s
          ON CAST(s.sensor_id AS TEXT) = CAST(r.sensor_id AS TEXT)
        WHERE CAST(s.sensor_id AS TEXT) = ?1
          AND (s.sensor_name = ?2 COLLATE NOCASE OR s.sensor_type = ?2 COLLATE NOCASE)
          AND date(r.recorded_at) = ?3
        ORDER BY datetime(r.recorded_at) ASC, r.id ASC
    )SQL";

    sqlite3_stmt *statement = nullptr;
    const int prepare_result = sqlite3_prepare_v2(database, query, -1, &statement, nullptr);
    if (prepare_result != SQLITE_OK) {
        error = sqlite3_errmsg(database);
        sqlite3_close(database);
        return false;
    }

    sqlite3_bind_text(statement, 1, sensor_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, sensor_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, date.c_str(), -1, SQLITE_TRANSIENT);

    while (true) {
        const int step_result = sqlite3_step(statement);
        if (step_result == SQLITE_ROW) {
            SensorLogValue item;
            item.recorded_at = sqlite_text(statement, 0);
            item.time = time_part(item.recorded_at);
            item.value = sqlite_text(statement, 1);
            values.push_back(std::move(item));
        } else if (step_result == SQLITE_DONE) {
            break;
        } else {
            error = sqlite3_errmsg(database);
            sqlite3_finalize(statement);
            sqlite3_close(database);
            return false;
        }
    }

    sqlite3_finalize(statement);
    sqlite3_close(database);
    return true;
}

}  // namespace

SensorLogStore::SensorLogStore(std::vector<std::string> database_paths)
    : database_paths_(std::move(database_paths)) {}

const std::vector<std::string> &SensorLogStore::database_paths() const noexcept {
    return database_paths_;
}

bool SensorLogStore::query(
    const std::string &sensor_name,
    const std::string &sensor_id,
    const std::string &date,
    SensorLogQueryResult &result,
    std::string &error
) const {
    result = SensorLogQueryResult{};
    result.databases_checked = database_paths_.size();

    if (database_paths_.empty()) {
        error = "no SQLite database path has been configured";
        return false;
    }

    std::vector<SensorLogValue> all_values;

    for (const std::string &database_path : database_paths_) {
        std::vector<SensorLogValue> database_values;
        std::string database_error;

        if (query_one_database(
                database_path,
                sensor_name,
                sensor_id,
                date,
                database_values,
                database_error
            )) {
            ++result.databases_succeeded;
            all_values.insert(
                all_values.end(),
                std::make_move_iterator(database_values.begin()),
                std::make_move_iterator(database_values.end())
            );
        } else {
            result.warnings.push_back(database_path + ": " + database_error);
        }
    }

    if (result.databases_succeeded == 0) {
        error = "none of the configured SQLite databases could be queried";
        return false;
    }

    std::sort(
        all_values.begin(),
        all_values.end(),
        [](const SensorLogValue &left, const SensorLogValue &right) {
            if (left.recorded_at != right.recorded_at) {
                return left.recorded_at < right.recorded_at;
            }
            return left.value < right.value;
        }
    );

    std::set<std::pair<std::string, std::string>> seen;
    for (SensorLogValue &item : all_values) {
        const auto key = std::make_pair(item.recorded_at, item.value);
        if (seen.insert(key).second) {
            result.values.push_back(std::move(item));
        }
    }

    return true;
}

std::string json_escape(const std::string &text) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');

    for (const unsigned char character : text) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20) {
                    output << "\\u" << std::setw(4) << static_cast<unsigned int>(character);
                } else {
                    output << static_cast<char>(character);
                }
        }
    }

    return output.str();
}

std::string build_sensor_log_json(
    const std::string &sensor_name,
    const std::string &sensor_id,
    const std::string &date,
    const SensorLogQueryResult &result,
    bool found
) {
    std::ostringstream output;
    output << "{\n"
           << "  \"sensor_name\": \"" << json_escape(sensor_name) << "\",\n"
           << "  \"sensor_id\": \"" << json_escape(sensor_id) << "\",\n"
           << "  \"date\": \"" << json_escape(date) << "\",\n"
           << "  \"values\": [";

    if (!result.values.empty()) {
        output << '\n';
        for (std::size_t index = 0; index < result.values.size(); ++index) {
            const SensorLogValue &item = result.values[index];
            output << "    {\n"
                   << "      \"time\": \"" << json_escape(item.time) << "\",\n"
                   << "      \"value\": \"" << json_escape(item.value) << "\"\n"
                   << "    }";
            if (index + 1 < result.values.size()) {
                output << ',';
            }
            output << '\n';
        }
        output << "  ";
    }

    output << "]";

    if (!found) {
        output << ",\n  \"message\": \"No data found for the requested sensor and date\"";
    }

    if (!result.warnings.empty()) {
        output << ",\n  \"warnings\": [\n";
        for (std::size_t index = 0; index < result.warnings.size(); ++index) {
            output << "    \"" << json_escape(result.warnings[index]) << "\"";
            if (index + 1 < result.warnings.size()) {
                output << ',';
            }
            output << '\n';
        }
        output << "  ]";
    }

    output << "\n}\n";
    return output.str();
}
