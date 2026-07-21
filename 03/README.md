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
│   ├── mqtt_test.sh
│   ├── mqtt_speed_test.sh
│   ├── cache_speed_test.sh
│   └── show_logs.sh
│
├── logs/
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
    libpaho-mqtt-dev
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

`MQTT_VERSION=4` means MQTT 3.1.1.

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

Clearing the Master cache before Round 1 is required when the test must demonstrate a true cache miss.

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
MQTT callback registered
MQTT subscribe topic: hotel/sensors/request rc=0
MQTT connected
Master running on port 8000
MQTT Broker: 127.0.0.1:1883
MQTT Version: 3.1.1, QoS: 1
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

Error response:

```json
{
  "sensor_id": "999",
  "source": "",
  "success": false,
  "error": "sensor reading not found",
  "response_time_us": 2500
}
```

The `source` field can be:

| Source | Meaning |
|---|---|
| `cache` | Returned from Master Memcached |
| `master` | Returned from Master SQLite |
| `slave1` | Found through Slave1 |
| `slave2` | Found through Slave2 |
| empty | Request failed or the sensor was not found |

---

## 12. Sending Requests with `mosquitto_pub`

Run this command on the Master node, or from another machine that can reach the Broker:

```bash
mosquitto_pub \
    -h 127.0.0.1 \
    -p 1883 \
    -q 1 \
    -t "hotel/sensors/request" \
    -m '{"sensor_type":"temperature","sensor_id":"101"}'
```

From another machine, replace `127.0.0.1` with the Master IP:

```bash
mosquitto_pub \
    -h 192.168.122.22 \
    -p 1883 \
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
    -q 1 \
    -t "hotel/sensors/response/#" \
    -v
```

Subscribe only to sensor `101`:

```bash
mosquitto_sub \
    -h 127.0.0.1 \
    -p 1883 \
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

This script is intended for a basic MQTT request/response verification.

For manual testing, use three terminals:

Terminal 1 — Master:

```bash
cd ~/master
./master config
```

Terminal 2 — Subscriber:

```bash
mosquitto_sub \
    -t "hotel/sensors/response/#" \
    -v
```

Terminal 3 — Publisher:

```bash
mosquitto_pub \
    -t "hotel/sensors/request" \
    -m '{"sensor_type":"temperature","sensor_id":"101"}'
```

---

## 16. MQTT Two-Round Performance Test

The required benchmark script is:

```text
scripts/mqtt_speed_test.sh
```

It sends requests for multiple sensors in two rounds.

Tested sensors:

| Sensor ID | Sensor type |
|---:|---|
| `101` | `temperature` |
| `102` | `humidity` |
| `103` | `motion` |
| `104` | `temperature` |

Make the script executable:

```bash
chmod +x scripts/mqtt_speed_test.sh
```

Clear the Master cache before the benchmark:

```bash
printf "flush_all\r\nquit\r\n" | nc 127.0.0.1 11211
```

Run the benchmark:

```bash
./scripts/mqtt_speed_test.sh
```

The expected behavior is:

```text
Round 1: source = master, slave1, or slave2
Round 2: source = cache
```

Round 1 reads from SQLite or a Slave node and populates the Master cache. Round 2 repeats the same requests and should obtain the values from Memcached.

---

## 17. Measured Performance Results

The following results were obtained from the completed multi-sensor MQTT benchmark.

### Round 1 — Cache Miss

| Sensor ID | Type | Source | Response time |
|---:|---|---|---:|
| `101` | temperature | master | `1422 us` |
| `102` | humidity | master | `1471 us` |
| `103` | motion | master | `1137 us` |
| `104` | temperature | master | `1161 us` |

Average:

```text
(1422 + 1471 + 1137 + 1161) / 4 = 1297.75 us
```

### Round 2 — Cache Hit

| Sensor ID | Type | Source | Response time |
|---:|---|---|---:|
| `101` | temperature | cache | `300 us` |
| `102` | humidity | cache | `152 us` |
| `103` | motion | cache | `199 us` |
| `104` | temperature | cache | `193 us` |

Average:

```text
(300 + 152 + 199 + 193) / 4 = 211 us
```

### Comparison

```text
1297.75 / 211 ≈ 6.15
```

In this test, the cache-hit round was approximately `6.15` times faster on average than the cache-miss round.

The comparison must use `response_time_us` from the Master response. Measuring the complete Bash process also includes subscriber startup, process scheduling, and MQTT client connection overhead, which can hide the real cache improvement.

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

Run it from the `03/` directory:

```bash
./scripts/build_and_run.sh
```

Because the applications run on separate virtual machines, verify the hostnames, IP addresses, remote paths, and SSH connectivity expected by the script before execution.

Manual compilation and execution commands in this README can always be used when testing each node independently.

---

## 19. Other Utility Scripts

| Script | Purpose |
|---|---|
| `init_all_databases.sh` | Initializes Master and Slave databases from CSV data |
| `check_databases.sh` | Verifies database files, tables, and sensor records |
| `test_requests.sh` | Tests the HTTP request path |
| `mqtt_test.sh` | Performs a basic MQTT request/response test |
| `mqtt_speed_test.sh` | Runs the required two-round multi-sensor MQTT benchmark |
| `cache_speed_test.sh` | Retains the Part 2 HTTP/cache benchmark |
| `show_logs.sh` | Displays application logs |
| `build_and_run.sh` | Compiles and starts the programs according to the project workflow |

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

Clear Memcached before running the benchmark:

```bash
printf "flush_all\r\nquit\r\n" | nc 127.0.0.1 11211
```

Also verify that `CACHE_TTL` has not retained values from a previous test.

### Empty source in benchmark output

The requested `(sensor_id, sensor_type)` pair may not exist. List valid Master sensors:

```bash
sqlite3 ~/master/master.db \
"SELECT DISTINCT
    s.sensor_id,
    s.sensor_type,
    s.sensor_name
 FROM sensor_readings AS r
 JOIN sensors AS s
   ON r.sensor_id = s.sensor_id;"
```

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

---

## 22. Final Verification Checklist

Before submitting Part 3, verify that:

- [ ] Mosquitto is installed and running on the Master node.
- [ ] Memcached is running on Master, Slave1, and Slave2.
- [ ] All databases are initialized from CSV files.
- [ ] Master and Slave programs compile using Makefiles.
- [ ] `build_and_run.sh` exists and is executable.
- [ ] Master connects to the MQTT Broker.
- [ ] Master subscribes to `hotel/sensors/request`.
- [ ] Requests can be sent using `mosquitto_pub`.
- [ ] Responses can be received using `mosquitto_sub`.
- [ ] Response topics follow `hotel/sensors/response/<sensor_id>`.
- [ ] `mqtt_speed_test.sh` tests all required sensors in two rounds.
- [ ] Round 1 uses SQLite or a Slave node.
- [ ] Round 2 returns `source=cache`.
- [ ] Response times are recorded and compared.
- [ ] No IP address, port, or database path is hard-coded in C++ source files.
- [ ] `README.md` and `report.md` are present.
- [ ] The report contains an architecture or request-flow diagram.
- [ ] Build artifacts and temporary files are removed before submission.