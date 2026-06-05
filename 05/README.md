# Section 05 — Sensor Log API with Mongoose

## 1. Overview

This section implements an HTTP API for reading all recorded values of a sensor on a specified date. The server is written in C++17 and uses the Mongoose networking library to handle HTTP requests. Sensor metadata and readings are retrieved from a local SQLite database.

The API receives the following input parameters:

- `sensor_name`: the sensor type or the stored sensor name
- `sensor_id`: the unique sensor identifier
- `date`: the requested date in `YYYY-MM-DD` format

The API returns the matching sensor values in chronological order as a readable JSON document. When no data exists for the requested sensor and date, the server returns an empty `values` array and a descriptive message.

In the tested deployment, the API runs on the Master VM and reads the Master SQLite database. The Host machine sends HTTP requests to the Master VM through the local network.

---

## 2. Main Features

- HTTP server implemented with Mongoose
- C++17 implementation
- SQLite data access
- Query parameters for sensor name, sensor ID, and date
- JSON responses
- Date validation, including leap-year checking
- Parameter validation
- Prepared SQLite statements
- Read-only database access
- Chronological result ordering
- Duplicate result removal when multiple databases are configured
- Proper HTTP status codes
- Health-check endpoint
- Runtime configuration for listen address, port, and database path
- Makefile-based compilation
- Bash script for compilation and execution
- Core database test with a temporary SQLite database
- API test script using `curl`

---

## 3. Project Structure

```text
05/
├── api/
│   ├── main.cpp
│   ├── mongoose/
│   │   ├── mongoose.c
│   │   └── mongoose.h
│   ├── sensor_log_store.cpp
│   └── sensor_log_store.hpp
├── config/
│   ├── api.env
│   └── api.env.example
├── Makefile
├── scripts/
│   ├── build_and_run.sh
│   ├── create_test_db.sh
│   ├── fetch_mongoose.sh
│   └── test_api.sh
└── tests/
    └── test_store.cpp
```

### File descriptions

`api/main.cpp` initializes the Mongoose event manager, starts the HTTP listener, validates incoming requests, selects the correct route, and sends JSON responses.

`api/sensor_log_store.cpp` contains the SQLite reading logic. It opens each configured database in read-only mode, executes a prepared SQL query, sorts the readings by time, removes duplicate records, and prepares the result for JSON serialization.

`api/sensor_log_store.hpp` declares the data structures and database-access interface used by the API and the test program.

`api/mongoose/mongoose.c` and `api/mongoose/mongoose.h` are the Mongoose library source files compiled directly with the project.

`config/api.env` contains the active runtime configuration.

`config/api.env.example` is a configuration template.

`Makefile` compiles the API and the core database test.

`scripts/build_and_run.sh` loads the configuration, checks the Mongoose files, compiles the project, and starts the server.

`scripts/create_test_db.sh` creates a temporary SQLite database with sample sensor data for the core test.

`scripts/fetch_mongoose.sh` downloads the configured Mongoose release when the source files are not already available.

`scripts/test_api.sh` sends a health-check request and a sample sensor-log request to the running server.

`tests/test_store.cpp` tests the SQLite data-access layer independently from the HTTP server.

---

## 4. Requirements

The tested environment is Ubuntu 22.04.

Install the required packages:

```bash
sudo apt update
sudo apt install -y build-essential g++ make sqlite3 libsqlite3-dev curl
```

The following tools are required:

- GNU C++ compiler with C++17 support
- GNU Make
- SQLite 3 command-line utility
- SQLite development library
- `curl` for API tests
- Bash

Mongoose is included in the repository under `api/mongoose/`. Therefore, Internet access is not required for normal compilation when `mongoose.c` and `mongoose.h` already exist.

---

## 5. Configuration

The default configuration file is:

```text
config/api.env
```

Example configuration:

```bash
API_LISTEN_URL=http://0.0.0.0:8005
DATABASE_PATHS=/home/amir/master/master.db
MONGOOSE_VERSION=7.22
```

### Configuration variables

#### `API_LISTEN_URL`

Defines the HTTP listen address and port.

Examples:

```bash
API_LISTEN_URL=http://0.0.0.0:8005
```

This configuration accepts connections on all IPv4 interfaces.

```bash
API_LISTEN_URL=http://127.0.0.1:8005
```

This configuration only accepts requests from the local machine.

#### `DATABASE_PATHS`

Defines one or more SQLite database paths. Multiple paths must be separated with a colon.

Single database example:

```bash
DATABASE_PATHS=/home/amir/master/master.db
```

Multiple database example:

```bash
DATABASE_PATHS=/path/to/master.db:/path/to/second.db
```

The current deployment uses the Master database:

```text
/home/amir/master/master.db
```

The database path is not hard-coded in the C++ source code. It is passed to the executable as a command-line argument by `build_and_run.sh`.

#### `MONGOOSE_VERSION`

Defines the Mongoose release used by `scripts/fetch_mongoose.sh`.

The tested version is:

```bash
MONGOOSE_VERSION=7.22
```

### Creating the active configuration

When starting from the example file:

```bash
cp config/api.env.example config/api.env
nano config/api.env
```

Update the listen URL and database path for the current machine before running the program.

---

## 6. Expected SQLite Database Structure

The API expects the following tables.

### `sensors`

```sql
CREATE TABLE sensors (
    sensor_id TEXT PRIMARY KEY,
    sensor_type TEXT NOT NULL,
    sensor_name TEXT NOT NULL,
    location TEXT,
    unit TEXT,
    node_name TEXT NOT NULL,
    is_active INTEGER NOT NULL DEFAULT 1
);
```

### `sensor_readings`

```sql
CREATE TABLE sensor_readings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    sensor_id TEXT NOT NULL,
    value TEXT NOT NULL,
    recorded_at TEXT NOT NULL,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (sensor_id) REFERENCES sensors(sensor_id)
);
```

The API joins `sensor_readings` with `sensors` using `sensor_id`.

A request matches a sensor when:

- the provided `sensor_id` equals `sensors.sensor_id`, and
- the provided `sensor_name` equals either `sensors.sensor_name` or `sensors.sensor_type`, using a case-insensitive comparison, and
- the date portion of `sensor_readings.recorded_at` equals the requested date.

Results are ordered by `recorded_at` from earliest to latest.

---

## 7. Downloading Mongoose When Required

The project already contains the Mongoose source files. To download them again using the configured release:

```bash
cd /path/to/embedded_hw04/05
MONGOOSE_VERSION=7.22 ./scripts/fetch_mongoose.sh
```

The script downloads:

```text
api/mongoose/mongoose.c
api/mongoose/mongoose.h
api/mongoose/LICENSE
```

This step requires Internet access and either `curl` or `wget`.

---

## 8. Compilation

Move to the section directory:

```bash
cd /path/to/embedded_hw04/05
```

### Compile the API

```bash
make all
```

The executable is created at:

```text
api/sensor_log_api
```

### Clean generated files

```bash
make clean
```

This removes:

```text
api/sensor_log_api
tests/test_store
tests/test_sensor.db
```

### Verify the executable

```bash
ls -lh api/sensor_log_api
file api/sensor_log_api
ldd api/sensor_log_api | grep sqlite
```

---

## 9. Core Database Test

The core test checks the SQLite data-access layer without starting the HTTP server.

Run:

```bash
make clean
make test-core
```

The Makefile performs the following steps:

1. Compiles `tests/test_store.cpp` and `api/sensor_log_store.cpp`.
2. Runs `scripts/create_test_db.sh`.
3. Creates `tests/test_sensor.db`.
4. Queries sensor `101` for `2026-06-01`.
5. Verifies the two expected readings.

Expected output:

```json
{
  "sensor_name": "temperature",
  "sensor_id": "101",
  "date": "2026-06-01",
  "values": [
    {
      "time": "10:15:00",
      "value": "24.8"
    },
    {
      "time": "10:30:00",
      "value": "25.1"
    }
  ]
}
```

The temporary test database is independent of the real Master database.

---

## 10. Running the API

### Recommended method

Run the build-and-start script:

```bash
cd /home/amir/05
./scripts/build_and_run.sh
```

The script:

1. Loads `config/api.env`.
2. validates `API_LISTEN_URL` and `DATABASE_PATHS`.
3. downloads Mongoose only when its source files are missing.
4. runs `make clean all`.
5. converts every configured database path into a `--db` argument.
6. starts the API server in the foreground.

Expected startup output:

```text
Sensor log API is listening on http://0.0.0.0:8005
SQLite source: /home/amir/master/master.db
```

Keep this terminal open while testing the API.

Stop the server with:

```text
Ctrl+C
```

The program handles `SIGINT` and `SIGTERM` and releases the Mongoose event manager before exiting.

### Using a different configuration file

```bash
./scripts/build_and_run.sh /absolute/path/to/custom.env
```

### Manual execution

After compilation, the server can also be started directly:

```bash
./api/sensor_log_api \
  --listen http://0.0.0.0:8005 \
  --db /home/amir/master/master.db
```

Multiple databases can be supplied by repeating `--db`:

```bash
./api/sensor_log_api \
  --listen http://0.0.0.0:8005 \
  --db /path/to/first.db \
  --db /path/to/second.db
```

Display command-line usage:

```bash
./api/sensor_log_api --help
```

---

## 11. API Endpoints

### 11.1 Health Check

```http
GET /health
```

Local test:

```bash
curl -i http://127.0.0.1:8005/health
```

Successful response:

```http
HTTP/1.1 200 OK
Content-Type: application/json; charset=utf-8
Cache-Control: no-store
```

```json
{
  "status": "ok"
}
```

### 11.2 Read Sensor Logs

```http
GET /api/sensor-logs
```

Required query parameters:

| Parameter | Description | Example |
|---|---|---|
| `sensor_name` | Sensor type or stored sensor name | `temperature` |
| `sensor_id` | Sensor identifier | `101` |
| `date` | Requested date in `YYYY-MM-DD` format | `2026-06-01` |

Example request URL:

```text
/api/sensor-logs?sensor_name=temperature&sensor_id=101&date=2026-06-01
```

Recommended `curl` command:

```bash
curl -i --get 'http://127.0.0.1:8005/api/sensor-logs' \
  --data-urlencode 'sensor_name=temperature' \
  --data-urlencode 'sensor_id=101' \
  --data-urlencode 'date=2026-06-01'
```

Successful response:

```http
HTTP/1.1 200 OK
```

```json
{
  "sensor_name": "temperature",
  "sensor_id": "101",
  "date": "2026-06-01",
  "values": [
    {
      "time": "10:00:00",
      "value": "24.2"
    },
    {
      "time": "10:15:00",
      "value": "24.8"
    }
  ]
}
```

The API also accepts the stored sensor name. Example:

```bash
curl -i --get 'http://127.0.0.1:8005/api/sensor-logs' \
  --data-urlencode 'sensor_name=Floor1_Room101_Temp' \
  --data-urlencode 'sensor_id=101' \
  --data-urlencode 'date=2026-06-01'
```

---

## 12. HTTP Status Codes and Error Responses

### `200 OK`

The request is valid and at least one matching value was found.

### `400 Bad Request`

One or more required parameters are missing:

```bash
curl -i --get 'http://127.0.0.1:8005/api/sensor-logs' \
  --data-urlencode 'sensor_name=temperature' \
  --data-urlencode 'sensor_id=101'
```

Response:

```json
{
  "error": "sensor_name, sensor_id and date query parameters are required"
}
```

A date is also rejected when it is not a valid calendar date:

```bash
curl -i --get 'http://127.0.0.1:8005/api/sensor-logs' \
  --data-urlencode 'sensor_name=temperature' \
  --data-urlencode 'sensor_id=101' \
  --data-urlencode 'date=2026-02-30'
```

Response:

```json
{
  "error": "date must be a valid YYYY-MM-DD value"
}
```

### `404 Not Found`

The request is valid, but no matching reading exists:

```bash
curl -i --get 'http://127.0.0.1:8005/api/sensor-logs' \
  --data-urlencode 'sensor_name=temperature' \
  --data-urlencode 'sensor_id=101' \
  --data-urlencode 'date=2026-06-02'
```

Response:

```json
{
  "sensor_name": "temperature",
  "sensor_id": "101",
  "date": "2026-06-02",
  "values": [],
  "message": "No data found for the requested sensor and date"
}
```

An unknown route also returns `404`:

```bash
curl -i http://127.0.0.1:8005/api/unknown
```

```json
{
  "error": "Route not found"
}
```

### `405 Method Not Allowed`

The API endpoints only accept `GET` requests.

Example:

```bash
curl -i -X POST http://127.0.0.1:8005/api/sensor-logs
```

Response:

```json
{
  "error": "Only the GET method is allowed"
}
```

The response also includes:

```http
Allow: GET
```

### `500 Internal Server Error`

This status is returned when none of the configured databases can be queried, for example when every configured path is invalid or unreadable.

Example response:

```json
{
  "error": "none of the configured SQLite databases could be queried"
}
```

When at least one configured database succeeds and another fails, the response remains usable and contains a `warnings` array describing the failed database path.

---

## 13. Running the API Test Script

Start the API in the first terminal:

```bash
cd /home/amir/05
./scripts/build_and_run.sh
```

In a second terminal on the Master VM, run:

```bash
cd /home/amir/05
./scripts/test_api.sh
```

The script reads `API_LISTEN_URL` from `config/api.env`, converts wildcard addresses such as `0.0.0.0` to `127.0.0.1` for the local client request, and sends:

1. `GET /health`
2. `GET /api/sensor-logs` for sensor `101` on `2026-06-01`

A different configuration file may be supplied:

```bash
./scripts/test_api.sh /absolute/path/to/custom.env
```

The sample values in `test_api.sh` are test inputs only. They are not embedded in the API implementation. When the provided database dataset changes, update the test request values to match an existing sensor and date.

---

## 14. Testing from the Host Machine

The API is configured to listen on `0.0.0.0`, so it can be reached through the Master VM IP address when the Host and the VM are on the same network.

First, find the Master IP inside the Master VM:

```bash
hostname -I
```

Example Master IP used during testing:

```text
192.168.122.22
```

From the Host, test the API:

```bash
curl -i --get 'http://192.168.122.22:8005/api/sensor-logs' \
  --data-urlencode 'sensor_name=temperature' \
  --data-urlencode 'sensor_id=101' \
  --data-urlencode 'date=2026-06-01'
```

This verifies the complete path:

```text
Host operator
    |
    | HTTP GET over the local network
    v
Master VM: Mongoose API on port 8005
    |
    | Prepared read-only SQLite query
    v
/home/amir/master/master.db
    |
    | JSON response
    v
Host operator
```

Do not assume that the example IP is permanent. Check the VM IP before testing and use the current address.

---

## 15. Verifying the Listening Port

On the Master VM:

```bash
ss -ltnp | grep ':8005'
```

Expected result:

```text
LISTEN 0 128 0.0.0.0:8005 0.0.0.0:*
```

When the server has been stopped, the command should return no result.

---

## 16. Tested Sensor Requests

The following requests were successfully tested against the Master database for `2026-06-01`.

### Sensor 101 — temperature

```bash
curl -sS --get 'http://127.0.0.1:8005/api/sensor-logs' \
  --data-urlencode 'sensor_name=temperature' \
  --data-urlencode 'sensor_id=101' \
  --data-urlencode 'date=2026-06-01'
```

Returned values:

```text
10:00:00 -> 24.2
10:15:00 -> 24.8
```

### Sensor 102 — humidity

```bash
curl -sS --get 'http://127.0.0.1:8005/api/sensor-logs' \
  --data-urlencode 'sensor_name=humidity' \
  --data-urlencode 'sensor_id=102' \
  --data-urlencode 'date=2026-06-01'
```

Returned values:

```text
10:00:00 -> 44
10:15:00 -> 45
```

### Sensor 103 — motion

```bash
curl -sS --get 'http://127.0.0.1:8005/api/sensor-logs' \
  --data-urlencode 'sensor_name=motion' \
  --data-urlencode 'sensor_id=103' \
  --data-urlencode 'date=2026-06-01'
```

Returned values:

```text
10:10:00 -> 0
10:16:00 -> 1
```

### Sensor 104 — temperature

```bash
curl -sS --get 'http://127.0.0.1:8005/api/sensor-logs' \
  --data-urlencode 'sensor_name=temperature' \
  --data-urlencode 'sensor_id=104' \
  --data-urlencode 'date=2026-06-01'
```

Returned values:

```text
10:05:00 -> 23.5
10:20:00 -> 23.9
```

These values are examples from the tested database and are not hard-coded in the program.

---

## 17. Data Flow

The request-processing path is:

```text
HTTP client
    |
    | GET /api/sensor-logs
    | sensor_name + sensor_id + date
    v
Mongoose event loop
    |
    v
Route and method validation
    |
    v
Query-parameter extraction
    |
    v
Calendar-date validation
    |
    v
SensorLogStore
    |
    v
SQLite opened in read-only mode
    |
    v
Prepared SQL query
    |
    v
Rows ordered by recorded_at
    |
    v
Duplicate removal
    |
    v
JSON serialization
    |
    v
HTTP response
```

The SQL statement uses bound parameters instead of concatenating user input into the query. This reduces the risk of SQL injection.

---

## 18. Runtime and Error-Handling Behavior

- The server requires at least one `--db` argument.
- The server requires a `--listen` argument.
- The database is opened with `SQLITE_OPEN_READONLY`.
- SQLite busy timeout is set to 3000 milliseconds.
- A database failure is recorded as a warning when another configured database succeeds.
- A complete database failure returns HTTP `500`.
- Missing query parameters return HTTP `400`.
- Invalid calendar dates return HTTP `400`.
- A valid query with no matching records returns HTTP `404`.
- Unsupported HTTP methods return HTTP `405`.
- Unknown routes return HTTP `404`.
- JSON strings are escaped before being returned.
- Responses include `Cache-Control: no-store` to prevent caching of sensor-log responses by clients or intermediaries.

---

## 19. Security Notes

This implementation is suitable for the controlled local-network environment of the assignment. The current server uses plain HTTP and does not authenticate clients.

For a production deployment, the following improvements are recommended:

- enable HTTPS/TLS
- add authentication and authorization
- restrict the listen interface when remote access is not required
- apply firewall rules to the API port
- run the process with a dedicated unprivileged user
- limit request size and request rate
- validate sensor identifiers according to the production naming policy
- protect configuration files and database file permissions
- record access and error logs

The database query itself uses prepared statements and read-only access, which reduces the risk of SQL injection and accidental database modification.

---

## 20. Troubleshooting

### Mongoose sources are missing

Error:

```text
Mongoose sources are missing.
Run: ./scripts/fetch_mongoose.sh
```

Solution:

```bash
MONGOOSE_VERSION=7.22 ./scripts/fetch_mongoose.sh
make all
```

### Configuration file is missing

Error:

```text
Configuration file not found
```

Solution:

```bash
cp config/api.env.example config/api.env
nano config/api.env
```

### Database cannot be opened

Check the configured path:

```bash
cat config/api.env
ls -lh /home/amir/master/master.db
```

Verify the tables:

```bash
sqlite3 /home/amir/master/master.db '.tables'
```

Expected tables include:

```text
sensors
sensor_readings
```

### Port 8005 is already in use

Check the process:

```bash
ss -ltnp | grep ':8005'
```

Stop the existing process or select a different port in `config/api.env`.

### The API works locally but not from the Host

Check that:

1. `API_LISTEN_URL` uses `0.0.0.0`, not `127.0.0.1`.
2. the Master VM has a valid network IP.
3. Host and Master are connected to the same virtual network.
4. the selected port is not blocked by a firewall.
5. the request uses the current Master IP.

Useful commands:

```bash
hostname -I
ss -ltnp | grep ':8005'
```

### No data is returned

Confirm that the exact combination of sensor ID, sensor name/type, and date exists:

```bash
sqlite3 -header -column /home/amir/master/master.db "
SELECT
    s.sensor_id,
    s.sensor_type,
    s.sensor_name,
    r.value,
    r.recorded_at
FROM sensors AS s
JOIN sensor_readings AS r ON s.sensor_id = r.sensor_id
ORDER BY r.recorded_at;
"
```

---

## 21. Complete Test Procedure

Use the following sequence for a clean final test.

### On the Master VM — Terminal 1

```bash
cd /home/amir/05
make clean
make test-core
make all
./scripts/build_and_run.sh
```

### On the Master VM — Terminal 2

```bash
curl -i http://127.0.0.1:8005/health

curl -i --get 'http://127.0.0.1:8005/api/sensor-logs' \
  --data-urlencode 'sensor_name=temperature' \
  --data-urlencode 'sensor_id=101' \
  --data-urlencode 'date=2026-06-01'

curl -i --get 'http://127.0.0.1:8005/api/sensor-logs' \
  --data-urlencode 'sensor_name=temperature' \
  --data-urlencode 'sensor_id=101' \
  --data-urlencode 'date=2026-06-02'

curl -i --get 'http://127.0.0.1:8005/api/sensor-logs' \
  --data-urlencode 'sensor_name=temperature' \
  --data-urlencode 'sensor_id=101' \
  --data-urlencode 'date=2026-02-30'

curl -i --get 'http://127.0.0.1:8005/api/sensor-logs' \
  --data-urlencode 'sensor_name=temperature' \
  --data-urlencode 'sensor_id=101'

curl -i http://127.0.0.1:8005/api/unknown
```

### On the Host

Replace `MASTER_IP` with the current Master VM IP:

```bash
curl -i --get 'http://MASTER_IP:8005/api/sensor-logs' \
  --data-urlencode 'sensor_name=temperature' \
  --data-urlencode 'sensor_id=101' \
  --data-urlencode 'date=2026-06-01'
```

A successful Host-to-Master request confirms that the API is reachable through the local network and that the complete data path is operational.

---

## 22. Final Verification Checklist

Before submission, verify the following items:

- `api/main.cpp` exists.
- `api/sensor_log_store.cpp` and `.hpp` exist.
- `api/mongoose/mongoose.c` and `.h` exist.
- `Makefile` successfully builds the API.
- `scripts/build_and_run.sh` is executable.
- `scripts/test_api.sh` is executable.
- `scripts/create_test_db.sh` is executable.
- `config/api.env.example` is included.
- no IP address, port, or database path is hard-coded in the C++ source.
- `make test-core` succeeds.
- `make all` succeeds.
- `/health` returns HTTP `200`.
- a valid sensor query returns HTTP `200` and recorded values.
- a valid query with no data returns HTTP `404` and a descriptive message.
- a missing parameter returns HTTP `400`.
- an invalid date returns HTTP `400`.
- an unsupported method returns HTTP `405`.
- an unknown route returns HTTP `404`.
- the API is accessible from the Host using the Master VM IP.

---

## 23. Tested Deployment Summary

The implementation was successfully compiled and tested with the following setup:

```text
Operating system: Ubuntu 22.04
Compiler: g++ 11.4.0
Make: GNU Make 4.3
SQLite: 3.37.2
Mongoose: 7.22
API listen address: http://0.0.0.0:8005
Master VM IP during testing: 192.168.122.22
Database: /home/amir/master/master.db
```

The deployment successfully returned sensor logs for sensors `101`, `102`, `103`, and `104`, handled missing data and invalid input correctly, and accepted requests from both the Master VM and the Host machine.
