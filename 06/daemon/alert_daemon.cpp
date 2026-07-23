#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::atomic_bool g_running{true};

struct Options {
    std::vector<std::string> source_databases;
    std::string alert_database;
    std::string rules_file;
    int interval_seconds = 0;
    bool once = false;
};

struct Rules {
    double temperature_max = 0.0;
    double temperature_min_valid = 0.0;
    double temperature_max_valid = 0.0;
    double humidity_min = 0.0;
    double humidity_max = 0.0;
    double humidity_min_valid = 0.0;
    double humidity_max_valid = 0.0;
    long long stale_after_seconds = 0;
    int check_interval_seconds = 0;
};

struct SensorReading {
    std::string sensor_id;
    std::string sensor_type;
    std::string sensor_name;
    std::string value;
    std::string recorded_at;
    std::string source_database;
    bool has_age = false;
    long long age_seconds = 0;
};

struct AlertCandidate {
    std::string type;
    std::string details;
};

const std::vector<std::string> kManagedAlertTypes = {
    "TEMPERATURE_HIGH",
    "HUMIDITY_LOW",
    "HUMIDITY_HIGH",
    "STALE_DATA",
    "INVALID_VALUE"
};

void signal_handler(int) {
    g_running.store(false);
}

std::string trim(const std::string &text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string lower_copy(std::string text) {
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return text;
}

std::string sqlite_text(sqlite3_stmt *statement, int column) {
    const unsigned char *value = sqlite3_column_text(statement, column);
    return value == nullptr ? "" : reinterpret_cast<const char *>(value);
}

std::string utc_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm result{};
#if defined(_WIN32)
    gmtime_s(&result, &value);
#else
    gmtime_r(&value, &result);
#endif
    std::ostringstream output;
    output << std::put_time(&result, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

void log_line(const std::string &level, const std::string &message) {
    std::cout << utc_now() << " [" << level << "] " << message << std::endl;
}

void print_usage(const char *program) {
    std::cerr
        << "Usage: " << program
        << " --source-db <sqlite-path> [--source-db <sqlite-path> ...]"
        << " --alert-db <sqlite-path> --rules <rules-file>"
        << " [--interval <seconds>] [--once]\n";
}

bool parse_positive_int(const std::string &text, int &value) {
    try {
        std::size_t consumed = 0;
        const long parsed = std::stol(text, &consumed);
        if (consumed != text.size() || parsed <= 0 || parsed > 86400) {
            return false;
        }
        value = static_cast<int>(parsed);
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool parse_arguments(int argc, char **argv, Options &options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];

        if (argument == "--source-db" && index + 1 < argc) {
            options.source_databases.emplace_back(argv[++index]);
        } else if (argument == "--alert-db" && index + 1 < argc) {
            options.alert_database = argv[++index];
        } else if (argument == "--rules" && index + 1 < argc) {
            options.rules_file = argv[++index];
        } else if (argument == "--interval" && index + 1 < argc) {
            if (!parse_positive_int(argv[++index], options.interval_seconds)) {
                std::cerr << "Invalid --interval value.\n";
                return false;
            }
        } else if (argument == "--once") {
            options.once = true;
        } else if (argument == "--help" || argument == "-h") {
            print_usage(argv[0]);
            return false;
        } else {
            std::cerr << "Unknown or incomplete argument: " << argument << '\n';
            print_usage(argv[0]);
            return false;
        }
    }

    if (options.source_databases.empty() ||
        options.alert_database.empty() ||
        options.rules_file.empty()) {
        print_usage(argv[0]);
        return false;
    }

    return true;
}

bool parse_double(const std::string &text, double &value) {
    try {
        std::size_t consumed = 0;
        value = std::stod(text, &consumed);
        return consumed == text.size() && std::isfinite(value);
    } catch (const std::exception &) {
        return false;
    }
}

bool parse_long_long(const std::string &text, long long &value) {
    try {
        std::size_t consumed = 0;
        value = std::stoll(text, &consumed);
        return consumed == text.size();
    } catch (const std::exception &) {
        return false;
    }
}

bool load_rules(const std::string &path, Rules &rules, std::string &error) {
    std::ifstream input(path);
    if (!input) {
        error = "could not open rules file: " + path;
        return false;
    }

    std::map<std::string, std::string> values;
    std::string line;
    int line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            error = "invalid rules line " + std::to_string(line_number);
            return false;
        }

        const std::string key = trim(line.substr(0, separator));
        const std::string value = trim(line.substr(separator + 1));
        if (key.empty() || value.empty()) {
            error = "invalid rules line " + std::to_string(line_number);
            return false;
        }
        values[key] = value;
    }

    auto require_double = [&](const char *key, double &destination) -> bool {
        const auto iterator = values.find(key);
        if (iterator == values.end() || !parse_double(iterator->second, destination)) {
            error = std::string("missing or invalid rule: ") + key;
            return false;
        }
        return true;
    };

    auto require_integer = [&](const char *key, long long &destination) -> bool {
        const auto iterator = values.find(key);
        if (iterator == values.end() || !parse_long_long(iterator->second, destination)) {
            error = std::string("missing or invalid rule: ") + key;
            return false;
        }
        return true;
    };

    long long interval = 0;
    if (!require_double("TEMPERATURE_MAX", rules.temperature_max) ||
        !require_double("TEMPERATURE_MIN_VALID", rules.temperature_min_valid) ||
        !require_double("TEMPERATURE_MAX_VALID", rules.temperature_max_valid) ||
        !require_double("HUMIDITY_MIN", rules.humidity_min) ||
        !require_double("HUMIDITY_MAX", rules.humidity_max) ||
        !require_double("HUMIDITY_MIN_VALID", rules.humidity_min_valid) ||
        !require_double("HUMIDITY_MAX_VALID", rules.humidity_max_valid) ||
        !require_integer("STALE_AFTER_SECONDS", rules.stale_after_seconds) ||
        !require_integer("CHECK_INTERVAL_SECONDS", interval)) {
        return false;
    }

    if (rules.temperature_min_valid >= rules.temperature_max_valid ||
        rules.humidity_min_valid >= rules.humidity_max_valid ||
        rules.humidity_min >= rules.humidity_max ||
        rules.stale_after_seconds <= 0 || interval <= 0 || interval > 86400) {
        error = "rules contain an invalid range or non-positive interval";
        return false;
    }

    rules.check_interval_seconds = static_cast<int>(interval);
    return true;
}

bool initialize_alert_database(
    const std::string &path,
    sqlite3 **database,
    std::string &error
) {
    const int open_result = sqlite3_open_v2(
        path.c_str(),
        database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
        nullptr
    );
    if (open_result != SQLITE_OK) {
        error = *database == nullptr ? "could not open alert database" : sqlite3_errmsg(*database);
        if (*database != nullptr) {
            sqlite3_close(*database);
            *database = nullptr;
        }
        return false;
    }

    sqlite3_busy_timeout(*database, 5000);

    const char *schema = R"SQL(
        PRAGMA journal_mode = WAL;
        PRAGMA synchronous = NORMAL;

        CREATE TABLE IF NOT EXISTS alerts (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            sensor_id TEXT NOT NULL,
            sensor_name TEXT NOT NULL,
            alert_type TEXT NOT NULL,
            sensor_value TEXT NOT NULL,
            created_at TEXT NOT NULL,
            status TEXT NOT NULL,
            source_recorded_at TEXT NOT NULL,
            source_db TEXT NOT NULL,
            details TEXT NOT NULL,
            UNIQUE(sensor_id, sensor_name, alert_type, source_recorded_at, source_db)
        );

        CREATE INDEX IF NOT EXISTS idx_alerts_status_created
            ON alerts(status, created_at);

        CREATE INDEX IF NOT EXISTS idx_alerts_sensor
            ON alerts(sensor_id, sensor_name);
    )SQL";

    char *sqlite_error = nullptr;
    const int result = sqlite3_exec(*database, schema, nullptr, nullptr, &sqlite_error);
    if (result != SQLITE_OK) {
        error = sqlite_error == nullptr ? sqlite3_errmsg(*database) : sqlite_error;
        sqlite3_free(sqlite_error);
        sqlite3_close(*database);
        *database = nullptr;
        return false;
    }

    return true;
}

bool load_latest_readings(
    const std::string &database_path,
    std::vector<SensorReading> &readings,
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
        error = database == nullptr ? "could not open source database" : sqlite3_errmsg(database);
        if (database != nullptr) {
            sqlite3_close(database);
        }
        return false;
    }

    sqlite3_busy_timeout(database, 3000);

    const char *query = R"SQL(
        SELECT
            CAST(s.sensor_id AS TEXT),
            COALESCE(s.sensor_type, ''),
            COALESCE(s.sensor_name, ''),
            CAST(r.value AS TEXT),
            r.recorded_at,
            CASE
                WHEN strftime('%s', r.recorded_at) IS NULL THEN NULL
                ELSE CAST(strftime('%s', 'now') AS INTEGER)
                   - CAST(strftime('%s', r.recorded_at) AS INTEGER)
            END AS age_seconds
        FROM sensors AS s
        JOIN sensor_readings AS r
          ON r.id = (
              SELECT r2.id
              FROM sensor_readings AS r2
              WHERE CAST(r2.sensor_id AS TEXT) = CAST(s.sensor_id AS TEXT)
              ORDER BY datetime(r2.recorded_at) DESC, r2.id DESC
              LIMIT 1
          )
        WHERE COALESCE(s.is_active, 1) = 1
        ORDER BY s.sensor_id
    )SQL";

    sqlite3_stmt *statement = nullptr;
    const int prepare_result = sqlite3_prepare_v2(database, query, -1, &statement, nullptr);
    if (prepare_result != SQLITE_OK) {
        error = sqlite3_errmsg(database);
        sqlite3_close(database);
        return false;
    }

    while (true) {
        const int step_result = sqlite3_step(statement);
        if (step_result == SQLITE_ROW) {
            SensorReading reading;
            reading.sensor_id = sqlite_text(statement, 0);
            reading.sensor_type = sqlite_text(statement, 1);
            reading.sensor_name = sqlite_text(statement, 2);
            reading.value = sqlite_text(statement, 3);
            reading.recorded_at = sqlite_text(statement, 4);
            reading.source_database = database_path;
            reading.has_age = sqlite3_column_type(statement, 5) != SQLITE_NULL;
            if (reading.has_age) {
                reading.age_seconds = sqlite3_column_int64(statement, 5);
                if (reading.age_seconds < 0) {
                    reading.age_seconds = 0;
                }
            }
            readings.push_back(std::move(reading));
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

bool numeric_value(const std::string &text, double &value) {
    const std::string cleaned = trim(text);
    if (cleaned.empty()) {
        return false;
    }

    char *end = nullptr;
    errno = 0;
    value = std::strtod(cleaned.c_str(), &end);
    return errno == 0 && end != cleaned.c_str() && *end == '\0' && std::isfinite(value);
}

std::vector<AlertCandidate> evaluate(
    const SensorReading &reading,
    const Rules &rules
) {
    std::vector<AlertCandidate> alerts;

    if (!reading.has_age) {
        alerts.push_back({"INVALID_VALUE", "recorded_at is not a valid SQLite date/time"});
    } else if (reading.age_seconds > rules.stale_after_seconds) {
        alerts.push_back({
            "STALE_DATA",
            "latest reading age is " + std::to_string(reading.age_seconds) +
                " seconds; limit is " + std::to_string(rules.stale_after_seconds)
        });
    }

    double value = 0.0;
    if (!numeric_value(reading.value, value)) {
        alerts.push_back({"INVALID_VALUE", "sensor value is not a finite numeric value"});
        return alerts;
    }

    const std::string classification = lower_copy(
        reading.sensor_type + " " + reading.sensor_name
    );

    const bool is_temperature =
        classification.find("temperature") != std::string::npos ||
        classification.find("temp") != std::string::npos;
    const bool is_humidity =
        classification.find("humidity") != std::string::npos ||
        classification.find("humid") != std::string::npos;

    if (is_temperature) {
        if (value < rules.temperature_min_valid || value > rules.temperature_max_valid) {
            alerts.push_back({
                "INVALID_VALUE",
                "temperature is outside the valid range [" +
                    std::to_string(rules.temperature_min_valid) + ", " +
                    std::to_string(rules.temperature_max_valid) + "]"
            });
        } else if (value > rules.temperature_max) {
            alerts.push_back({
                "TEMPERATURE_HIGH",
                "temperature is greater than " + std::to_string(rules.temperature_max)
            });
        }
    }

    if (is_humidity) {
        if (value < rules.humidity_min_valid || value > rules.humidity_max_valid) {
            alerts.push_back({
                "INVALID_VALUE",
                "humidity is outside the valid range [" +
                    std::to_string(rules.humidity_min_valid) + ", " +
                    std::to_string(rules.humidity_max_valid) + "]"
            });
        } else {
            if (value < rules.humidity_min) {
                alerts.push_back({
                    "HUMIDITY_LOW",
                    "humidity is lower than " + std::to_string(rules.humidity_min)
                });
            }
            if (value > rules.humidity_max) {
                alerts.push_back({
                    "HUMIDITY_HIGH",
                    "humidity is greater than " + std::to_string(rules.humidity_max)
                });
            }
        }
    }

    return alerts;
}

bool begin_transaction(sqlite3 *database, std::string &error) {
    char *message = nullptr;
    if (sqlite3_exec(database, "BEGIN IMMEDIATE", nullptr, nullptr, &message) != SQLITE_OK) {
        error = message == nullptr ? sqlite3_errmsg(database) : message;
        sqlite3_free(message);
        return false;
    }
    return true;
}

void rollback(sqlite3 *database) {
    sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr);
}

bool commit(sqlite3 *database, std::string &error) {
    char *message = nullptr;
    if (sqlite3_exec(database, "COMMIT", nullptr, nullptr, &message) != SQLITE_OK) {
        error = message == nullptr ? sqlite3_errmsg(database) : message;
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool insert_alert(
    sqlite3 *database,
    const SensorReading &reading,
    const AlertCandidate &candidate,
    bool &inserted,
    std::string &error
) {
    const char *sql = R"SQL(
        INSERT OR IGNORE INTO alerts(
            sensor_id,
            sensor_name,
            alert_type,
            sensor_value,
            created_at,
            status,
            source_recorded_at,
            source_db,
            details
        ) VALUES(?1, ?2, ?3, ?4, ?5, 'OPEN', ?6, ?7, ?8)
    )SQL";

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(database);
        return false;
    }

    const std::string sensor_name = reading.sensor_name.empty()
        ? reading.sensor_type
        : reading.sensor_name;
    const std::string created_at = utc_now();

    sqlite3_bind_text(statement, 1, reading.sensor_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, sensor_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, candidate.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, reading.value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 6, reading.recorded_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 7, reading.source_database.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 8, candidate.details.c_str(), -1, SQLITE_TRANSIENT);

    const int result = sqlite3_step(statement);
    if (result != SQLITE_DONE) {
        error = sqlite3_errmsg(database);
        sqlite3_finalize(statement);
        return false;
    }

    inserted = sqlite3_changes(database) > 0;
    sqlite3_finalize(statement);
    return true;
}

bool resolve_alert_type(
    sqlite3 *database,
    const SensorReading &reading,
    const std::string &alert_type,
    int &resolved_count,
    std::string &error
) {
    const char *sql = R"SQL(
        UPDATE alerts
        SET status = 'RESOLVED'
        WHERE sensor_id = ?1
          AND sensor_name = ?2
          AND alert_type = ?3
          AND status = 'OPEN'
          AND source_db = ?4
    )SQL";

    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(database);
        return false;
    }

    const std::string sensor_name = reading.sensor_name.empty()
        ? reading.sensor_type
        : reading.sensor_name;

    sqlite3_bind_text(statement, 1, reading.sensor_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, sensor_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, alert_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, reading.source_database.c_str(), -1, SQLITE_TRANSIENT);

    const int result = sqlite3_step(statement);
    if (result != SQLITE_DONE) {
        error = sqlite3_errmsg(database);
        sqlite3_finalize(statement);
        return false;
    }

    resolved_count += sqlite3_changes(database);
    sqlite3_finalize(statement);
    return true;
}

bool process_reading(
    sqlite3 *alert_database,
    const SensorReading &reading,
    const Rules &rules,
    int &inserted_count,
    int &resolved_count,
    std::string &error
) {
    const std::vector<AlertCandidate> candidates = evaluate(reading, rules);
    std::set<std::string> active_types;
    for (const AlertCandidate &candidate : candidates) {
        active_types.insert(candidate.type);
    }

    if (!begin_transaction(alert_database, error)) {
        return false;
    }

    for (const AlertCandidate &candidate : candidates) {
        bool inserted = false;
        if (!insert_alert(alert_database, reading, candidate, inserted, error)) {
            rollback(alert_database);
            return false;
        }
        if (inserted) {
            ++inserted_count;
            log_line(
                "ALERT",
                candidate.type + " sensor_id=" + reading.sensor_id +
                    " value=" + reading.value + " source=" + reading.source_database
            );
        }
    }

    for (const std::string &managed_type : kManagedAlertTypes) {
        if (active_types.count(managed_type) == 0) {
            if (!resolve_alert_type(
                    alert_database,
                    reading,
                    managed_type,
                    resolved_count,
                    error
                )) {
                rollback(alert_database);
                return false;
            }
        }
    }

    if (!commit(alert_database, error)) {
        rollback(alert_database);
        return false;
    }

    return true;
}

bool run_cycle(
    sqlite3 *alert_database,
    const Options &options,
    const Rules &rules
) {
    std::vector<SensorReading> readings;
    int successful_sources = 0;

    for (const std::string &source : options.source_databases) {
        std::vector<SensorReading> source_readings;
        std::string error;
        if (load_latest_readings(source, source_readings, error)) {
            ++successful_sources;
            readings.insert(
                readings.end(),
                std::make_move_iterator(source_readings.begin()),
                std::make_move_iterator(source_readings.end())
            );
        } else {
            log_line("ERROR", source + ": " + error);
        }
    }

    if (successful_sources == 0) {
        log_line("ERROR", "no configured source database could be read");
        return false;
    }

    int inserted_count = 0;
    int resolved_count = 0;

    for (const SensorReading &reading : readings) {
        std::string error;
        if (!process_reading(
                alert_database,
                reading,
                rules,
                inserted_count,
                resolved_count,
                error
            )) {
            log_line("ERROR", "could not process sensor " + reading.sensor_id + ": " + error);
            return false;
        }
    }

    log_line(
        "INFO",
        "cycle complete: sensors=" + std::to_string(readings.size()) +
            " inserted=" + std::to_string(inserted_count) +
            " resolved=" + std::to_string(resolved_count)
    );
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    Options options;
    if (!parse_arguments(argc, argv, options)) {
        return 2;
    }

    Rules rules;
    std::string error;
    if (!load_rules(options.rules_file, rules, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    if (options.interval_seconds == 0) {
        options.interval_seconds = rules.check_interval_seconds;
    }

    sqlite3 *alert_database = nullptr;
    if (!initialize_alert_database(options.alert_database, &alert_database, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    log_line("INFO", "sensor alert daemon started");
    log_line("INFO", "alert database: " + options.alert_database);
    log_line("INFO", "check interval: " + std::to_string(options.interval_seconds) + " seconds");

    int exit_code = 0;
    do {
        if (!run_cycle(alert_database, options, rules)) {
            exit_code = 1;
        }

        if (options.once || !g_running.load()) {
            break;
        }

        for (int elapsed = 0;
             elapsed < options.interval_seconds && g_running.load();
             ++elapsed) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } while (g_running.load());

    sqlite3_close(alert_database);
    log_line("INFO", "sensor alert daemon stopped");
    return exit_code;
}
