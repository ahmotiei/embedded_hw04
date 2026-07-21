#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char *kDefaultBaseOid = ".1.3.6.1.4.1.8072.9999.4";

struct Options {
    std::string database_path;
    std::string node_name;
    std::string base_oid = kDefaultBaseOid;
    std::string operation;
    std::string requested_oid;
};

struct Oid {
    std::vector<unsigned long> parts;
};

struct VariableBinding {
    Oid oid;
    std::string oid_text;
    std::string type;
    std::string value;
};

std::string trim(const std::string &text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string one_line(std::string text) {
    for (char &character : text) {
        if (character == '\r' || character == '\n' || character == '\t') {
            character = ' ';
        }
    }
    return trim(text);
}

std::string sqlite_text(sqlite3_stmt *statement, int column) {
    const unsigned char *value = sqlite3_column_text(statement, column);
    return value == nullptr
        ? ""
        : reinterpret_cast<const char *>(value);
}

bool parse_oid(const std::string &raw, Oid &oid) {
    std::string text = trim(raw);
    while (!text.empty() && text.front() == '.') {
        text.erase(text.begin());
    }
    while (!text.empty() && text.back() == '.') {
        text.pop_back();
    }

    if (text.empty()) {
        return false;
    }

    Oid parsed;
    std::stringstream stream(text);
    std::string token;

    while (std::getline(stream, token, '.')) {
        if (token.empty()) {
            return false;
        }

        for (const unsigned char character : token) {
            if (!std::isdigit(character)) {
                return false;
            }
        }

        try {
            std::size_t consumed = 0;
            const unsigned long value = std::stoul(token, &consumed);
            if (consumed != token.size()) {
                return false;
            }
            parsed.parts.push_back(value);
        } catch (const std::exception &) {
            return false;
        }
    }

    if (parsed.parts.empty()) {
        return false;
    }

    oid = std::move(parsed);
    return true;
}

std::string oid_to_string(const Oid &oid) {
    std::ostringstream output;
    for (const unsigned long part : oid.parts) {
        output << '.' << part;
    }
    return output.str();
}

int compare_oid(const Oid &left, const Oid &right) {
    const std::size_t common = std::min(left.parts.size(), right.parts.size());

    for (std::size_t index = 0; index < common; ++index) {
        if (left.parts[index] < right.parts[index]) {
            return -1;
        }
        if (left.parts[index] > right.parts[index]) {
            return 1;
        }
    }

    if (left.parts.size() < right.parts.size()) {
        return -1;
    }
    if (left.parts.size() > right.parts.size()) {
        return 1;
    }
    return 0;
}

bool starts_with_oid(const Oid &value, const Oid &prefix) {
    if (value.parts.size() < prefix.parts.size()) {
        return false;
    }

    return std::equal(prefix.parts.begin(), prefix.parts.end(), value.parts.begin());
}

bool parse_arguments(int argc, char **argv, Options &options) {
    const char *database_from_env = std::getenv("SENSOR_DB_PATH");
    const char *node_from_env = std::getenv("SENSOR_NODE_NAME");
    const char *base_from_env = std::getenv("SENSOR_BASE_OID");

    if (database_from_env != nullptr) {
        options.database_path = database_from_env;
    }
    if (node_from_env != nullptr) {
        options.node_name = node_from_env;
    }
    if (base_from_env != nullptr && *base_from_env != '\0') {
        options.base_oid = base_from_env;
    }

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];

        if (argument == "--db" && index + 1 < argc) {
            options.database_path = argv[++index];
        } else if (argument == "--node" && index + 1 < argc) {
            options.node_name = argv[++index];
        } else if (argument == "--base-oid" && index + 1 < argc) {
            options.base_oid = argv[++index];
        } else if ((argument == "-g" || argument == "-n") && index + 1 < argc) {
            options.operation = argument;
            options.requested_oid = argv[++index];
        } else if (argument == "-s") {
            options.operation = argument;
            if (index + 1 < argc) {
                options.requested_oid = argv[++index];
            }
            // Net-SNMP appends TYPE and VALUE after the OID for SET. Consume
            // them when present; the subtree is read-only, so they are not used.
            if (index + 1 < argc) {
                ++index;
            }
            if (index + 1 < argc) {
                ++index;
            }
        } else {
            std::cerr << "Unknown or incomplete argument: " << argument << '\n';
            return false;
        }
    }

    if (options.database_path.empty()) {
        std::cerr << "Database path is missing. Use --db or SENSOR_DB_PATH.\n";
        return false;
    }

    if (options.operation.empty() || options.requested_oid.empty()) {
        std::cerr << "SNMP pass operation is missing. Expected -g, -n, or -s.\n";
        return false;
    }

    Oid parsed_base;
    if (!parse_oid(options.base_oid, parsed_base)) {
        std::cerr << "Invalid base OID: " << options.base_oid << '\n';
        return false;
    }
    options.base_oid = oid_to_string(parsed_base);

    return true;
}

void add_binding(
    std::vector<VariableBinding> &bindings,
    const std::string &oid_text,
    const std::string &type,
    const std::string &value
) {
    Oid oid;
    if (!parse_oid(oid_text, oid)) {
        throw std::runtime_error("Internal invalid OID: " + oid_text);
    }

    bindings.push_back({oid, oid_to_string(oid), type, one_line(value)});
}

std::string make_description(
    const std::string &sensor_type,
    const std::string &location,
    const std::string &node_name
) {
    std::ostringstream description;
    description << "type=" << sensor_type;

    if (!location.empty()) {
        description << "; location=" << location;
    }

    if (!node_name.empty()) {
        description << "; node=" << node_name;
    }

    return description.str();
}

bool load_bindings(
    const Options &options,
    std::vector<VariableBinding> &bindings,
    std::string &error
) {
    sqlite3 *database = nullptr;

    const int open_result = sqlite3_open_v2(
        options.database_path.c_str(),
        &database,
        SQLITE_OPEN_READONLY,
        nullptr
    );

    if (open_result != SQLITE_OK) {
        error = database == nullptr
            ? "Could not open SQLite database"
            : sqlite3_errmsg(database);

        if (database != nullptr) {
            sqlite3_close(database);
        }
        return false;
    }

    const char *query = R"SQL(
        SELECT
            s.sensor_id,
            s.sensor_type,
            s.sensor_name,
            COALESCE(s.location, ''),
            COALESCE(s.unit, ''),
            COALESCE(s.node_name, ''),
            r.value,
            r.recorded_at
        FROM sensors AS s
        JOIN sensor_readings AS r
          ON r.id = (
              SELECT r2.id
              FROM sensor_readings AS r2
              WHERE r2.sensor_id = s.sensor_id
              ORDER BY r2.recorded_at DESC, r2.id DESC
              LIMIT 1
          )
        WHERE COALESCE(s.is_active, 1) = 1
        ORDER BY
            CASE
                WHEN s.sensor_id GLOB '[0-9]*' THEN CAST(s.sensor_id AS INTEGER)
                ELSE 2147483647
            END,
            s.sensor_id
    )SQL";

    sqlite3_stmt *statement = nullptr;
    const int prepare_result = sqlite3_prepare_v2(
        database,
        query,
        -1,
        &statement,
        nullptr
    );

    if (prepare_result != SQLITE_OK) {
        error = sqlite3_errmsg(database);
        sqlite3_close(database);
        return false;
    }

    struct SensorRow {
        std::string sensor_id;
        std::string sensor_type;
        std::string sensor_name;
        std::string location;
        std::string unit;
        std::string node_name;
        std::string value;
        std::string recorded_at;
    };

    std::vector<SensorRow> rows;

    while (true) {
        const int step_result = sqlite3_step(statement);

        if (step_result == SQLITE_ROW) {
            SensorRow row;
            row.sensor_id = sqlite_text(statement, 0);
            row.sensor_type = sqlite_text(statement, 1);
            row.sensor_name = sqlite_text(statement, 2);
            row.location = sqlite_text(statement, 3);
            row.unit = sqlite_text(statement, 4);
            row.node_name = sqlite_text(statement, 5);
            row.value = sqlite_text(statement, 6);
            row.recorded_at = sqlite_text(statement, 7);

            if (row.node_name.empty()) {
                row.node_name = options.node_name;
            }

            rows.push_back(std::move(row));
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

    const std::string &base = options.base_oid;
    add_binding(bindings, base + ".1.0", "integer", std::to_string(rows.size()));

    for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
        const SensorRow &row = rows[row_index];
        const std::string index = std::to_string(row_index + 1);
        const std::string entry = base + ".2.1";

        add_binding(bindings, entry + ".1." + index, "integer", index);
        add_binding(bindings, entry + ".2." + index, "string", row.sensor_id);
        add_binding(bindings, entry + ".3." + index, "string", row.sensor_name);
        add_binding(
            bindings,
            entry + ".4." + index,
            "string",
            make_description(row.sensor_type, row.location, row.node_name)
        );
        add_binding(bindings, entry + ".5." + index, "string", row.value);
        add_binding(bindings, entry + ".6." + index, "string", row.unit);
        add_binding(bindings, entry + ".7." + index, "string", row.recorded_at);
        add_binding(bindings, entry + ".8." + index, "string", row.sensor_type);
        add_binding(bindings, entry + ".9." + index, "string", row.node_name);
    }

    std::sort(
        bindings.begin(),
        bindings.end(),
        [](const VariableBinding &left, const VariableBinding &right) {
            return compare_oid(left.oid, right.oid) < 0;
        }
    );

    return true;
}

void print_binding(const VariableBinding &binding) {
    std::cout << binding.oid_text << '\n';
    std::cout << binding.type << '\n';
    std::cout << binding.value << '\n';
}

int process_get(
    const Options &options,
    const std::vector<VariableBinding> &bindings
) {
    Oid requested;
    if (!parse_oid(options.requested_oid, requested)) {
        std::cout << "NONE\n";
        return 0;
    }

    for (const VariableBinding &binding : bindings) {
        if (compare_oid(binding.oid, requested) == 0) {
            print_binding(binding);
            return 0;
        }
    }

    std::cout << "NONE\n";
    return 0;
}

int process_get_next(
    const Options &options,
    const std::vector<VariableBinding> &bindings
) {
    Oid requested;
    if (!parse_oid(options.requested_oid, requested)) {
        std::cout << "NONE\n";
        return 0;
    }

    Oid base;
    parse_oid(options.base_oid, base);

    // A GETNEXT that starts before the subtree is allowed. A request that is
    // already after the subtree must terminate with NONE.
    if (!starts_with_oid(requested, base) && compare_oid(requested, base) > 0) {
        std::cout << "NONE\n";
        return 0;
    }

    for (const VariableBinding &binding : bindings) {
        if (compare_oid(binding.oid, requested) > 0) {
            print_binding(binding);
            return 0;
        }
    }

    std::cout << "NONE\n";
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    Options options;

    if (!parse_arguments(argc, argv, options)) {
        return 2;
    }

    if (options.operation == "-s") {
        std::cout << "not-writable\n";
        return 0;
    }

    std::vector<VariableBinding> bindings;
    std::string error;

    try {
        if (!load_bindings(options, bindings, error)) {
            std::cerr << "Could not load sensor data: " << error << '\n';
            std::cout << "NONE\n";
            return 1;
        }
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        std::cout << "NONE\n";
        return 1;
    }

    if (options.operation == "-g") {
        return process_get(options, bindings);
    }

    if (options.operation == "-n") {
        return process_get_next(options, bindings);
    }

    std::cout << "NONE\n";
    return 0;
}
