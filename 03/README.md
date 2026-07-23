# Part 3 — MQTT Integration for the Distributed Sensor Database

## Overview

This directory contains Part 3 of the distributed sensor database project. It extends the HTTP- and cache-based system from Parts 1 and 2 by adding MQTT communication.

The system contains:

- one Master node,
- two Slave nodes,
- one Mosquitto MQTT Broker,
- one local SQLite database on each node,
- one Memcached instance on each node,
- an HTTP API implemented with Mongoose,
- an MQTT bridge implemented with Eclipse Paho MQTT C,
- Bash scripts for initialization, execution, testing, and benchmarking.

The operator sends a sensor request to the MQTT Broker. The Master receives the request, performs the distributed lookup, and publishes the final result to a response topic.

The lookup order is:

```text
Master Memcached
       |
       | cache miss
       v
Master SQLite
       |
       | not found
       v
Slave1 HTTP API
       |
       | not found
       v
Slave2 HTTP API
```

Whenever data is obtained from SQLite or a Slave node, the Master stores the sensor JSON object in Memcached. A repeated request can therefore be answered directly from cache.

---

## 1. Project Structure

```text
03/
├── data/
│   ├── master_sensors.csv
│   ├── slave1_sensors.csv
│   └── slave2_sensors.csv
│
├── master/
│   ├── main.cpp
│   ├── mqtt_bridge.cpp
│   ├── mqtt_bridge.h
│   ├── sensor_service.cpp
│   ├── sensor_service.h
│   ├── sensor_service_internal.h
│   ├── sensor_service_backend.cpp
│   ├── sensor_service_backend.h
│   ├── types.h
│   ├── cache.cpp
│   ├── cache.h
│   ├── http_client.cpp
│   ├── http_client.h
│   ├── master_init_db.sh
│   ├── config
│   ├── config.example
│   └── Makefile
│
├── slave1/
│   ├── main.cpp
│   ├── cache.cpp
│   ├── cache.h
│   ├── slave1_init_db.sh
│   ├── slave_init_db.sh
│   ├── config
│   ├── config.example
│   └── Makefile
│
├── slave2/
│   ├── main.cpp
│   ├── cache.cpp
│   ├── cache.h
│   ├── slave2_init_db.sh
│   ├── slave_init_db.sh
│   ├── config
│   ├── config.example
│   └── Makefile
│
├── mongoose/
│   ├── mongoose.c
│   └── mongoose.h
│
├── mqtt/
│   └── mosquitto.conf
│
├── scripts/
│   ├── build_and_run.sh
│   ├── init_all_databases.sh
│   ├── check_databases.sh
│   ├── test_requests.sh
│   ├── mqtt_common.sh
│   ├── mqtt_test.sh
│   ├── mqtt_speed_test.sh
│   ├── cache_speed_test.sh
│   └── show_logs.sh
│
├── test-output/
│   └── README.md
├── logs/
├── CHANGELOG.md
├── README.md
└── report.md
```

Files such as `*.o`, the `master` executable, Slave executables, databases generated during testing, logs, and temporary result files are build/runtime artifacts and should not be committed to Git.

To remove Master build artifacts:

```bash
cd master
make clean
```

Do the same inside `slave1/` and `slave2/`.

---

## 2. System Architecture

```text
                         Operator / Test Script
                                  |
                                  | MQTT publish
                                  v
                     +---------------------------+
                     | Mosquitto MQTT Broker     |
                     | Master node, TCP 1883     |
                     +-------------+-------------+
                                   |
                                   | MQTT subscribe/publish
                                   v
                     +---------------------------+
                     | Master Application        |
                     | HTTP API: TCP 8000        |
                     | MQTT Bridge               |
                     | Sensor Service            |
                     | Memcached + SQLite        |
                     +-------------+-------------+
                                   |
                      HTTP request | 
                   +---------------+---------------+
                   |                               |
                   v                               v
        +-----------------------+       +-----------------------+
        | Slave1 Application    |       | Slave2 Application    |
        | HTTP API: TCP 9001    |       | HTTP API: TCP 9002    |
        | Memcached + SQLite    |       | Memcached + SQLite    |
        +-----------------------+       +-----------------------+
```

The operator communicates only with the MQTT Broker or the Master HTTP API. Slave nodes do not communicate directly with the operator.

---

## 3. Tested Network Configuration

The following addresses were used during development:

| Node | IP address | Application port |
|---|---:|---:|
| Master | `192.168.122.22` | `8000` |
| Slave1 | `192.168.122.18` | `9001` |
| Slave2 | `192.168.122.190` | `9002` |
| MQTT Broker | Master node | `1883` |

These values are not hard-coded in the source code. They are read from configuration files. If the virtual-machine addresses change, update the configuration files before running the system.

---

## 4. Dependencies

### 4.1 Master Node

Install the compiler, SQLite, Memcached, MQTT, and command-line tools:

```bash
sudo apt update

sudo apt install -y \
    build-essential \
    sqlite3 \
    libsqlite3-dev \
    memcached \
    libmemcached-dev \
    mosquitto \
    mosquitto-clients \
    libpaho-mqtt-dev \
    netcat-openbsd
```

### 4.2 Slave Nodes

Install the compiler, SQLite, and Memcached dependencies:

```bash
sudo apt update

sudo apt install -y \
    build-essential \
    sqlite3 \
    libsqlite3-dev \
    memcached \
    libmemcached-dev
```

The Slave applications do not need to connect directly to MQTT.

---

## 5. MQTT Broker Installation and Configuration

Mosquitto runs on the Master node.

Enable and start the system service:

```bash
sudo systemctl enable mosquitto
sudo systemctl start mosquitto
```

Check its status:

```bash
systemctl status mosquitto
```

The expected state is:

```text
Active: active (running)
```

### Project Broker Configuration

The project also contains:

```text
mqtt/mosquitto.conf
```

A development configuration can use the following settings:

```conf
listener 1883 0.0.0.0
protocol mqtt

allow_anonymous true

persistence false

connection_messages true
log_timestamp true
log_dest stdout
log_type all
```

To run Mosquitto manually with the project configuration:

```bash
sudo systemctl stop mosquitto

cd ~/03
mosquitto -c mqtt/mosquitto.conf -v
```

Do not run both the system Mosquitto service and a manual Mosquitto process on port `1883` at the same time.

Check whether the port is already in use:

```bash
sudo ss -ltnp | grep 1883
```

`allow_anonymous true` is acceptable only for the isolated laboratory network. A production deployment should use authentication, ACL rules, TLS, and port `8883`.

---

## 6. Application Configuration

### 6.1 Master Configuration

The Master reads its settings from:

```text
master/config
```

Example:

```ini
PORT=8000
DATABASE=master.db

SLAVE_TIMEOUT_MS=3000

SLAVE1_IP=192.168.122.18
SLAVE1_PORT=9001

SLAVE2_IP=192.168.122.190
SLAVE2_PORT=9002

MEMCACHED_HOST=127.0.0.1
MEMCACHED_PORT=11211
CACHE_TTL=300

MQTT_BROKER_HOST=127.0.0.1
MQTT_BROKER_PORT=1883
MQTT_CLIENT_ID=hotel-master
MQTT_REQUEST_TOPIC=hotel/sensors/request
MQTT_RESPONSE_PREFIX=hotel/sensors/response
MQTT_QOS=1
MQTT_VERSION=4
MQTT_KEEPALIVE=30
MQTT_RECONNECT_MS=3000
```

`MQTT_VERSION=4` means MQTT 3.1.1. The Master applies this version explicitly through the Paho connection option `MQTTVERSION_3_1_1`.

If the Broker connection is lost after startup, the Master retries after `MQTT_RECONNECT_MS` milliseconds and subscribes to the request topic again after reconnection. If the initial MQTT connection or subscription fails, the Master exits with an error instead of continuing without MQTT.

The selected QoS is:

```text
QoS 1 — At least once delivery
```

QoS 1 was selected because it reduces the probability of losing a sensor request or response during a short network interruption while producing less protocol overhead than QoS 2. Duplicate delivery is acceptable because the operation is read-only.

### 6.2 Slave Configuration

Each Slave reads its own port, database path, and Memcached settings from its local `config` file.

Example for Slave1:

```ini
PORT=9001
DATABASE=slave1.db
MEMCACHED_HOST=127.0.0.1
MEMCACHED_PORT=11211
CACHE_TTL=300
```

Example for Slave2:

```ini
PORT=9002
DATABASE=slave2.db
MEMCACHED_HOST=127.0.0.1
MEMCACHED_PORT=11211
CACHE_TTL=300
```

Use the actual database filenames defined in the current configuration files.

---

## 7. Database Initialization

The databases are created from CSV files in `data/`. Sensor IDs and values are not hard-coded in the C++ source code.

Initialize all databases from the project root:

```bash
cd ~/03

chmod +x scripts/init_all_databases.sh
./scripts/init_all_databases.sh
```

The individual initialization scripts are:

```text
master/master_init_db.sh
slave1/slave1_init_db.sh
slave2/slave2_init_db.sh
```

Check the resulting databases:

```bash
chmod +x scripts/check_databases.sh
./scripts/check_databases.sh
```

The main database tables are:

```text
node_info
sensors
sensor_readings
```

To inspect the Master database manually:

```bash
sqlite3 master/master.db ".tables"
```

Example query:

```bash
sqlite3 master/master.db \
"SELECT s.sensor_id, s.sensor_type, s.sensor_name
 FROM sensors AS s
 JOIN sensor_readings AS r
   ON r.sensor_id = s.sensor_id
 GROUP BY s.sensor_id, s.sensor_type, s.sensor_name;"
```

---

## 8. Starting Memcached

Start Memcached on every node:

```bash
sudo systemctl enable memcached
sudo systemctl start memcached
```

Check its status:

```bash
systemctl status memcached
```

To clear all keys before a benchmark:

```bash
printf "flush_all\r\nquit\r\n" | nc 127.0.0.1 11211
```

If `nc` is missing:

```bash
sudo apt install netcat-openbsd
```

The corrected `mqtt_speed_test.sh` and `mqtt_test.sh` scripts clear the Master cache automatically before starting. The manual command remains useful for troubleshooting or independent tests.

---

## 9. Compilation

### Master

```bash
cd ~/master
make clean
make
```

The Master link step uses:

```text
SQLite3
libmemcached
Eclipse Paho MQTT C
Mongoose
```

### Slave1

```bash
cd ~/slave1
make clean
make
```

### Slave2

```bash
cd ~/slave2
make clean
make
```

All programs must be compiled with their provided Makefiles.

---

## 10. Running the Programs

The recommended startup order is:

1. Start Memcached on all nodes.
2. Start Mosquitto on the Master node.
3. Start Slave1.
4. Start Slave2.
5. Start the Master application.
6. Start the MQTT subscriber or test script.

### 10.1 Run Slave1

```bash
ssh amir@192.168.122.18

cd ~/slave1
./slave1 config
```

Expected output includes:

```text
Slave running on port 9001
```

### 10.2 Run Slave2

```bash
ssh amir@192.168.122.190

cd ~/slave2
./slave2 config
```

Expected output includes:

```text
Slave running on port 9002
```

### 10.3 Run Master

```bash
ssh amir@192.168.122.22

cd ~/master
./master config
```

Expected startup output includes:

```text
MQTT connected and subscribed to hotel/sensors/request with QoS 1 using MQTT 3.1.1
Master running on port 8000, database: master.db, cache: enabled
MQTT Broker: 127.0.0.1:1883
MQTT Version: 3.1.1, QoS: 1, Keepalive: 30 seconds
```

The temporary `MQTT loop alive` debug message must not be present in the final version.

---

## 11. MQTT Topic Design

### Request Topic

```text
hotel/sensors/request
```

Every sensor request is published to this common topic.

Request payload:

```json
{
  "sensor_type": "temperature",
  "sensor_id": "101"
}
```

### Response Topic

```text
hotel/sensors/response/<sensor_id>
```

Example:

```text
hotel/sensors/response/101
```

Successful response:

```json
{
  "sensor_id": "101",
  "source": "master",
  "success": true,
  "data": {
    "sensor_id": "101",
    "sensor_type": "temperature",
    "sensor_name": "Floor1_Room101_Temp",
    "location": "Floor1_Room101",
    "value": "24.8",
    "unit": "C",
    "recorded_at": "2026-06-01 10:15:00"
  },
  "response_time_us": 1422
}
```

Cache-hit response:

```json
{
  "sensor_id": "101",
  "source": "cache",
  "success": true,
  "data": {
    "sensor_id": "101",
    "sensor_type": "temperature",
    "sensor_name": "Floor1_Room101_Temp",
    "location": "Floor1_Room101",
    "value": "24.8",
    "unit": "C",
    "recorded_at": "2026-06-01 10:15:00"
  },
  "response_time_us": 300
}
```

Sensor-not-found response:

```json
{
  "sensor_id": "999",
  "source": "not_found",
  "success": false,
  "error": "sensor reading not found",
  "response_time_us": 2500
}
```

If the distributed lookup cannot be completed because of a database, network, or Slave communication failure, the response uses `"source":"error"` instead of reporting the sensor as missing.

The `source` field can be:

| Source | Meaning |
|---|---|
| `cache` | Returned from Master Memcached |
| `master` | Returned from Master SQLite |
| `slave1` | Found through Slave1 |
| `slave2` | Found through Slave2 |
| `not_found` | The complete distributed lookup finished and the sensor was absent |
| `error` | The distributed lookup could not be completed |

---

## 12. Sending Requests with `mosquitto_pub`

Run this command on the Master node, or from another machine that can reach the Broker:

```bash
mosquitto_pub \
    -h 127.0.0.1 \
    -p 1883 \
    -V mqttv311 \
    -q 1 \
    -t "hotel/sensors/request" \
    -m '{"sensor_type":"temperature","sensor_id":"101"}'
```

From another machine, replace `127.0.0.1` with the Master IP:

```bash
mosquitto_pub \
    -h 192.168.122.22 \
    -p 1883 \
    -V mqttv311 \
    -q 1 \
    -t "hotel/sensors/request" \
    -m '{"sensor_type":"temperature","sensor_id":"101"}'
```

---

## 13. Receiving Responses with `mosquitto_sub`

Subscribe to all response topics:

```bash
mosquitto_sub \
    -h 127.0.0.1 \
    -p 1883 \
    -V mqttv311 \
    -q 1 \
    -t "hotel/sensors/response/#" \
    -v
```

Subscribe only to sensor `101`:

```bash
mosquitto_sub \
    -h 127.0.0.1 \
    -p 1883 \
    -V mqttv311 \
    -q 1 \
    -t "hotel/sensors/response/101" \
    -v
```

The `#` wildcard receives responses for all sensor IDs.

---

## 14. HTTP Compatibility Test

Part 3 preserves the HTTP API from the previous parts.

Example Master request:

```bash
curl \
"http://192.168.122.22:8000/api/sensor?type=temperature&id=101"
```

A request to `/` on a Slave returns:

```json
{"error":"route not found"}
```

This response confirms that the Slave process is reachable, but `/` is not an implemented endpoint. Use the actual sensor API path for a functional query.

---

## 15. MQTT Functional Test

The project contains:

```text
scripts/mqtt_test.sh
```

Make it executable:

```bash
chmod +x scripts/mqtt_test.sh
```

Run:

```bash
./scripts/mqtt_test.sh
```

The script reads the Broker address, port, request topic, response prefix, MQTT version, QoS, Memcached address, and timeout from `master/config`. It does not contain a fixed list of test sensors. Instead, it dynamically selects one sensor from each of the Master, Slave1, and Slave2 CSV files.

The functional test verifies all of the following cases:

```text
Master database lookup
Slave1 distributed lookup
Slave2 distributed lookup
Repeated request served from Master cache
Unknown sensor returns source=not_found
```

Before testing, the script flushes the Master cache. For every request, it subscribes only to the exact response topic of that sensor, applies MQTT 3.1.1 and the configured QoS, enforces a response timeout, and validates `success`, `sensor_id`, and `source`. The script exits with a nonzero status if any case fails.

An alternative Master configuration file can be passed as the first argument:

```bash
./scripts/mqtt_test.sh /path/to/master/config
```

For manual testing, use three terminals:

Terminal 1 — Master:

```bash
cd ~/master
./master config
```

Terminal 2 — Subscriber:

```bash
mosquitto_sub \
    -h 127.0.0.1 \
    -p 1883 \
    -V mqttv311 \
    -q 1 \
    -t "hotel/sensors/response/#" \
    -v
```

Terminal 3 — Publisher:

```bash
mosquitto_pub \
    -h 127.0.0.1 \
    -p 1883 \
    -V mqttv311 \
    -q 1 \
    -t "hotel/sensors/request" \
    -m '{"sensor_type":"temperature","sensor_id":"101"}'
```

---

## 16. MQTT Two-Round Performance Test

The required benchmark script is:

```text
scripts/mqtt_speed_test.sh
```

It dynamically discovers every unique `(sensor_id, sensor_type)` pair from all CSV files in `data/` and sends the same requests in two rounds. Therefore, sensors located on the Master, Slave1, and Slave2 are all included without hard-coding their IDs in the script.

Make the script executable:

```bash
chmod +x scripts/mqtt_speed_test.sh
```

Run the benchmark:

```bash
./scripts/mqtt_speed_test.sh
```

An alternative Master configuration file can be passed as the first argument:

```bash
./scripts/mqtt_speed_test.sh /path/to/master/config
```

The script flushes the Master cache automatically before Round 1. It applies MQTT 3.1.1 and the configured QoS to both `mosquitto_pub` and `mosquitto_sub`, subscribes to the exact response topic of each sensor, and uses a timeout so that a missing response cannot block the test indefinitely.

The expected behavior is:

```text
Round 1: source = master, slave1, or slave2
Round 2: source = cache
```

Round 1 reads from SQLite or a Slave node and populates the Master cache. Round 2 repeats the same dynamically discovered sensors and requires every successful response to have `source=cache`.

Two timing values are recorded for every request:

| Field | Meaning |
|---|---|
| `service_time_us` | Internal distributed lookup time reported by the Master as `response_time_us` |
| `round_trip_us` | End-to-end time from MQTT publish until the matching MQTT response is received |

The script generates:

```text
test-output/mqtt_speed_results.csv
test-output/mqtt_speed_summary.md
```

The CSV contains the result of every sensor in both rounds. The Markdown summary contains the average service time, average MQTT round-trip time, speedup ratio, and percentage reduction. The script exits with a nonzero status if a request fails, Round 1 unexpectedly uses cache, or Round 2 is not served from cache.

---

## 17. Measured Performance Results

The corrected benchmark no longer keeps manually copied timing numbers in the README. This prevents the README, terminal screenshots, and report from containing results from different runs.

After the final test, use the automatically generated files as the source of truth:

```text
test-output/mqtt_speed_results.csv
test-output/mqtt_speed_summary.md
```

### Round 1 — Cache Miss

Round 1 begins after the script successfully flushes the Master Memcached instance. Each successful response must come from `master`, `slave1`, or `slave2`. A Round 1 response with `source=cache` is treated as a validation failure.

### Round 2 — Cache Hit

Round 2 repeats exactly the same sensor requests. Each successful response must have `source=cache`; otherwise, the script reports a failure.

### Comparison

The generated summary contains the following comparison table with the real values from the latest execution:

```text
| Metric | Round 1 | Round 2 | Round 1 / Round 2 | Reduction |
| Master service time | generated value | generated value | generated value | generated value |
| MQTT end-to-end time | generated value | generated value | generated value | generated value |
```

`Master service time` measures the lookup path inside the Master application. `MQTT end-to-end time` also includes MQTT client connection, Broker delivery, process scheduling, and response reception. Both values are retained because they describe different parts of the system performance.

---

## 18. Build-and-Run Script

The project contains:

```text
scripts/build_and_run.sh
```

Make it executable:

```bash
chmod +x scripts/build_and_run.sh
```

Run it from the `03/` directory in the VM corresponding to the node:

```bash
./scripts/build_and_run.sh master [config-file]
./scripts/build_and_run.sh slave1 [config-file]
./scripts/build_and_run.sh slave2 [config-file]
```

The script does not try to start all three applications on one machine and does not require SSH. For the selected node, it initializes the local database from the corresponding CSV file, compiles the program with its Makefile, checks the local Memcached service, checks the MQTT Broker when the selected node is `master`, and then starts the program in the foreground.

Run the Slave1 command in the Slave1 VM, the Slave2 command in the Slave2 VM, and the Master command in the Master VM. The optional configuration path is useful when a file other than the default `master/config`, `slave1/config`, or `slave2/config` must be used.

---

## 19. Other Utility Scripts

| Script | Purpose |
|---|---|
| `init_all_databases.sh` | Initializes Master and Slave databases from CSV data |
| `check_databases.sh` | Verifies database files, tables, and sensor records |
| `test_requests.sh` | Tests the HTTP request path |
| `mqtt_common.sh` | Provides shared configuration, timeout, publish/subscribe, cache flush, JSON extraction, and sensor-discovery functions for MQTT tests |
| `mqtt_test.sh` | Validates Master, Slave1, Slave2, cache-hit, and unknown-sensor MQTT cases |
| `mqtt_speed_test.sh` | Dynamically tests all CSV sensors in two rounds and generates CSV and Markdown results |
| `cache_speed_test.sh` | Retains the Part 2 HTTP/cache benchmark |
| `show_logs.sh` | Displays application logs |
| `build_and_run.sh` | Initializes, compiles, validates services, and starts one selected node in its corresponding VM |

---

## 20. Troubleshooting

### `MQTTClient.h: No such file or directory`

Install Eclipse Paho MQTT C:

```bash
sudo apt install libpaho-mqtt-dev
```

### `Unit mosquitto.service could not be found`

Install Mosquitto:

```bash
sudo apt install mosquitto mosquitto-clients
```

### `Address already in use`

Check port `1883`:

```bash
sudo ss -ltnp | grep 1883
```

Stop the existing service before running Mosquitto manually:

```bash
sudo systemctl stop mosquitto
```

### Master connects to MQTT but does not receive requests

Verify:

```bash
mosquitto_sub -t "hotel/sensors/request" -v
```

Publish a test request:

```bash
mosquitto_pub \
    -t "hotel/sensors/request" \
    -m '{"sensor_type":"temperature","sensor_id":"101"}'
```

Confirm that the Master configuration points to the Broker running on the same node:

```ini
MQTT_BROKER_HOST=127.0.0.1
MQTT_BROKER_PORT=1883
```

### Slave request timeout

Check network access:

```bash
ping -c 2 192.168.122.18
ping -c 2 192.168.122.190
```

Check ports:

```bash
curl http://192.168.122.18:9001
curl http://192.168.122.190:9002
```

A `route not found` response confirms that the process and port are reachable.

### `no such table: sensor_readings`

Open the database inside the application directory, not an empty database accidentally created in the home directory:

```bash
sqlite3 ~/master/master.db ".tables"
```

### Round 1 unexpectedly returns `source=cache`

The corrected benchmark flushes the Master cache automatically and fails if Round 1 still returns `source=cache`. Verify that `MEMCACHED_HOST` and `MEMCACHED_PORT` in the selected Master configuration point to the same Memcached instance used by the running Master application.

A manual flush can still be used for diagnosis:

```bash
printf "flush_all\r\nquit\r\n" | nc 127.0.0.1 11211
```

### `source=error` or MQTT response timeout

`source=error` means the distributed lookup could not be completed, for example because SQLite failed or a Slave was unreachable. `source=not_found` means all nodes were checked successfully and the sensor did not exist.

A timeout means that the script did not receive a response on the exact sensor response topic within `RESPONSE_TIMEOUT_SECONDS`. Check the Broker, Master process, MQTT topics, MQTT version, QoS, and the Master-to-Slave network paths.

---

## 21. Security Notes

The development environment uses plain MQTT and HTTP inside an isolated virtual-machine network.

Current limitations:

- anonymous MQTT access,
- unencrypted MQTT traffic,
- unencrypted HTTP traffic,
- no client authentication,
- no per-topic authorization,
- no application-level request signature,
- no rate limiting.

Recommended production improvements:

- MQTT over TLS on port `8883`,
- username/password or client-certificate authentication,
- Mosquitto ACL rules,
- unique credentials for every client,
- HTTPS for Master-to-Slave communication,
- input-size limits and stricter JSON validation,
- request identifiers and duplicate-request handling,
- rate limiting and audit logging,
- firewall rules that expose only required ports.

