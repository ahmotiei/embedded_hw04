# Distributed Sensor Database System — Section 2: Caching Layer

## Overview

This section extends the distributed sensor database system developed in Section 1 by adding an in-memory cache based on Memcached. The system contains one Master node and two Slave nodes. Each node has its own SQLite database and its own local Memcached service.

The operator sends all requests to the Master node. For each request, the Master first checks its local cache. If the requested sensor reading is found, the response is returned immediately. If the value is not available in the cache, the Master checks its local SQLite database. If the reading does not belong to the Master, the request is forwarded to Slave1 and then to Slave2. After a successful lookup, the returned sensor data is stored in the Master cache so that later requests can be answered without accessing SQLite or contacting another node.

The virtual machines are managed through SSH. Application-level communication is performed using HTTP over the configured IP addresses and ports.

## Network Configuration

| Node | IP Address | HTTP Port |
|---|---:|---:|
| Master | `192.168.122.22` | `8000` |
| Slave1 | `192.168.122.18` | `9001` |
| Slave2 | `192.168.122.190` | `9002` |

## Project Structure

```text
02/
├── cache_speed_result.csv
├── cache_speed_summary.csv
├── data/
│   ├── master_sensors.csv
│   ├── slave1_sensors.csv
│   └── slave2_sensors.csv
├── logs/
│   ├── master.log
│   ├── slave1.log
│   └── slave2.log
├── master/
│   ├── cache.cpp
│   ├── cache.h
│   ├── config
│   ├── config.example
│   ├── http_client.cpp
│   ├── http_client.h
│   ├── main.cpp
│   ├── Makefile
│   ├── master.db
│   └── master_init_db.sh
├── mongoose/
│   ├── mongoose.c
│   └── mongoose.h
├── README.md
├── report.md
├── scripts/
│   ├── build_and_run.sh
│   ├── build_node.sh
│   ├── cache_speed_test.sh
│   ├── check_databases.sh
│   ├── clean_generated_files.sh
│   ├── common.sh
│   ├── flush_cache.sh
│   ├── init_all_databases.sh
│   ├── init_node_database.sh
│   ├── install_dependencies.sh
│   ├── run_master.sh
│   ├── run_slave1.sh
│   ├── run_slave2.sh
│   ├── show_logs.sh
│   ├── stop_node.sh
│   └── test_requests.sh
├── slave1/
│   ├── cache.cpp
│   ├── cache.h
│   ├── config
│   ├── config.example
│   ├── main.cpp
│   ├── Makefile
│   ├── slave1.db
│   └── slave1_init_db.sh
└── slave2/
    ├── cache.cpp
    ├── cache.h
    ├── config
    ├── config.example
    ├── main.cpp
    ├── Makefile
    ├── slave2.db
    └── slave2_init_db.sh
```

## Required Packages

Install the required packages on the Master, Slave1, and Slave2 virtual machines. The provided installation script installs the dependencies and enables Memcached:

```bash
bash scripts/install_dependencies.sh
```

The equivalent manual installation command is:

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    gcc \
    g++ \
    make \
    pkg-config \
    sqlite3 \
    libsqlite3-dev \
    memcached \
    libmemcached-dev \
    netcat-openbsd \
    curl \
    python3
```

Memcached provides the in-memory cache service. `libmemcached-dev` provides the C/C++ client library used by the programs. SQLite is used as persistent storage, and Mongoose is used to implement the HTTP servers and inter-node communication.

## Installing and Running Memcached

Memcached must be installed and running on all three virtual machines. The node startup scripts check the local Memcached endpoint and start the service automatically when it is installed but not running.

Memcached can also be enabled and started manually with:

```bash
sudo systemctl enable memcached
sudo systemctl start memcached
```

Check the service status with:

```bash
sudo systemctl status memcached --no-pager
```

In this project, Memcached listens only on the loopback interface:

```text
127.0.0.1:11211
```

This means that each application accesses only the Memcached instance installed on the same VM. The service is not exposed directly to the external network.

The listening address can be checked with:

```bash
sudo ss -lntp | grep 11211
```

A simple status test can be performed inside each VM:

```bash
echo "stats" | nc -q 1 localhost 11211
```

To clear the cache of a node, run the matching command locally on that node's VM from the root of Section 2:

```bash
bash scripts/flush_cache.sh master
bash scripts/flush_cache.sh slave1
bash scripts/flush_cache.sh slave2
```

For example, `flush_cache.sh slave1` must be executed on the Slave1 VM. A successful cache flush is reported by the script.

## Configuration Files

Each node reads its runtime settings from a configuration file. IP addresses, ports, database paths, cache settings, and timeouts are not hard-coded in the source code.

Before the first execution, copy each example file to the corresponding runtime configuration file:

```bash
cp master/config.example master/config
cp slave1/config.example slave1/config
cp slave2/config.example slave2/config
```

The real Slave IP addresses must then be set in `master/config`.

A typical Master configuration is:

```ini
PORT=8000
DATABASE=master.db

SLAVE1_IP=192.168.122.18
SLAVE1_PORT=9001

SLAVE2_IP=192.168.122.190
SLAVE2_PORT=9002

SLAVE_TIMEOUT_MS=3000

MEMCACHED_HOST=127.0.0.1
MEMCACHED_PORT=11211
CACHE_TTL=300
```

A typical Slave1 configuration is:

```ini
PORT=9001
DATABASE=slave1.db

MEMCACHED_HOST=127.0.0.1
MEMCACHED_PORT=11211
CACHE_TTL=300
```

A typical Slave2 configuration is:

```ini
PORT=9002
DATABASE=slave2.db

MEMCACHED_HOST=127.0.0.1
MEMCACHED_PORT=11211
CACHE_TTL=300
```

The cache key format is:

```text
sensor:<sensor_type>:<sensor_id>
```

For example:

```text
sensor:temperature:201
```

## Database Initialization

The SQLite databases can be initialized from the CSV files in the `data/` directory.

From the root of Section 2, initialize all three databases with:

```bash
bash scripts/init_all_databases.sh
```

A single node database can be initialized with:

```bash
bash scripts/init_node_database.sh master
bash scripts/init_node_database.sh slave1
bash scripts/init_node_database.sh slave2
```

The databases can be checked with:

```bash
bash scripts/check_databases.sh
```

The database contains the following main tables:

```text
sensors
sensor_readings
node_info
```

The `sensors` table stores sensor metadata. The `sensor_readings` table stores sensor values and timestamps.

## Compiling the Programs

Each node has its own Makefile. The applications must be compiled on their corresponding virtual machines.

From the root of Section 2, the provided build script can compile a selected node:

### Compile Master

On the Master VM:

```bash
cd ~/embedded/embedded_hw04/02
bash scripts/build_node.sh master
```

The Master build compiles and links `main.cpp`, `http_client.cpp`, `cache.cpp`, and `mongoose.c` with SQLite and libmemcached.

### Compile Slave1

On the Slave1 VM:

```bash
cd ~/embedded/embedded_hw04/02
bash scripts/build_node.sh slave1
```

### Compile Slave2

On the Slave2 VM:

```bash
cd ~/embedded/embedded_hw04/02
bash scripts/build_node.sh slave2
```

A successful build creates these executables:

```text
master/master
slave1/slave1
slave2/slave2
```

The `build_node.sh` script performs `make clean` before building. Use `--no-clean` only when a clean rebuild is not required.

## Running Master and Slave Nodes

The three programs must be running at the same time. Run every command from the root of Section 2 on the VM named in the subsection.

By default, each startup script initializes the node database from its CSV file, checks and starts the local Memcached service, flushes the local cache, performs a clean build, and runs the program in the foreground.

### Start Slave1

On the Slave1 VM:

```bash
cd ~/embedded/embedded_hw04/02
bash scripts/run_slave1.sh
```

Expected startup output includes:

```text
Slave running on port 9001, database: slave1.db, cache: enabled
```

### Start Slave2

On the Slave2 VM:

```bash
cd ~/embedded/embedded_hw04/02
bash scripts/run_slave2.sh
```

Expected startup output includes:

```text
Slave running on port 9002, database: slave2.db, cache: enabled
```

### Start Master

On the Master VM:

```bash
cd ~/embedded/embedded_hw04/02
bash scripts/run_master.sh
```

Expected startup output includes:

```text
Master running on port 8000, database: master.db, cache: enabled
Master Memcached: 127.0.0.1:11211, TTL: 300 seconds
Slave1: 192.168.122.18:9001
Slave2: 192.168.122.190:9002
```

The terminal running each server must remain open while tests are performed. To run a node in the background, add `--background`:

```bash
bash scripts/run_master.sh --background
```

Background processes can be stopped with:

```bash
bash scripts/stop_node.sh master
bash scripts/stop_node.sh slave1
bash scripts/stop_node.sh slave2
```

The startup scripts also support `--no-init`, `--no-clean`, and `--no-flush` when the corresponding default step must be skipped.

## Sending Requests

The operator sends requests only to the Master node.

The endpoint format is:

```text
GET /api/sensor?type=<sensor_type>&id=<sensor_id>
```

A request for a sensor stored on the Master can be sent from the host system with:

```bash
curl -s \
"http://192.168.122.22:8000/api/sensor?type=temperature&id=101" \
| python3 -m json.tool
```

A request for a sensor stored on Slave1 is:

```bash
curl -s \
"http://192.168.122.22:8000/api/sensor?type=temperature&id=201" \
| python3 -m json.tool
```

A request for a sensor stored on Slave2 is:

```bash
curl -s \
"http://192.168.122.22:8000/api/sensor?type=temperature&id=301" \
| python3 -m json.tool
```

Example response:

```json
{
    "source": "cache",
    "response_time_us": 73,
    "data": {
        "sensor_id": "201",
        "sensor_type": "temperature",
        "sensor_name": "Floor2_Room201_Temp",
        "location": "Floor2_Room201",
        "value": "25.3",
        "unit": "C",
        "recorded_at": "2026-06-01 10:15:00"
    }
}
```

The `source` field has the following meanings:

| Source | Meaning |
|---|---|
| `master` | Data was read from the Master SQLite database. |
| `slave1` | The request was forwarded to Slave1. |
| `slave2` | The request was forwarded to Slave2. |
| `cache` | Data was returned directly from the Master Memcached instance. |

## Cache Read Flow

The Master follows this lookup order:

```text
Client
  |
  v
Master Memcached
  |
  |-- Cache Hit --> Return response
  |
  |-- Cache Miss
          |
          v
      Master SQLite
          |
          |-- Found --> Store in Master cache --> Return response
          |
          |-- Not Found
                  |
                  v
               Slave1
                  |
                  |-- Found --> Store in Master cache --> Return response
                  |
                  |-- Not Found
                          |
                          v
                       Slave2
                          |
                          |-- Found --> Store in Master cache --> Return response
                          |
                          |-- Not Found --> Return HTTP 404
```

Each Slave also checks its own Memcached instance before reading its local SQLite database. In the final design, the Master cache is always checked before the Master database and before any request is forwarded to a Slave node.

## Running the Speed Test Script

The speed test script is located at:

```text
scripts/cache_speed_test.sh
```

The script reads every unique sensor dynamically from these files:

```text
data/master_sensors.csv
data/slave1_sensors.csv
data/slave2_sensors.csv
```

Therefore, no sensor ID, sensor type, Master IP address, or HTTP port is fixed inside the benchmark script. Before running the benchmark, make sure that Master, Slave1, and Slave2 are running.

The benchmark is normally executed on the Master VM. By default, it connects to `127.0.0.1` and reads the HTTP port from `master/config`:

```bash
cd ~/embedded/embedded_hw04/02
bash scripts/cache_speed_test.sh
```

The script automatically flushes the Master cache before round 1. For a fully cold distributed test, first run the matching command locally on each VM:

On the Master VM:

```bash
bash scripts/flush_cache.sh master
```

On the Slave1 VM:

```bash
bash scripts/flush_cache.sh slave1
```

On the Slave2 VM:

```bash
bash scripts/flush_cache.sh slave2
```

Then run the benchmark on the Master VM without another automatic flush:

```bash
bash scripts/cache_speed_test.sh --no-flush
```

When the test is executed from another machine, the Master address and port can be supplied without modifying the script:

```bash
bash scripts/cache_speed_test.sh --host 192.168.122.22 --port 8000
```

The detailed and summary results are saved in:

```text
cache_speed_result.csv
cache_speed_summary.csv
```

The detailed CSV file has this format:

```text
round,expected_node,sensor_id,sensor_type,http_status,source,server_response_time_us,client_total_time_us,value,recorded_at,error
```

The summary CSV contains the number of successful requests, cache hits, mean, median, minimum, and maximum server response times, and the mean client-side total time for each round.

## Benchmark Method

### Round 1 — Cache Miss

The Master cache is cleared before the benchmark. During the first round:

- Sensors owned by the Master are read from the Master SQLite database.
- Sensors owned by Slave1 are obtained through Slave1.
- Sensors owned by Slave2 are obtained through Slave2.
- Every successful result is stored in the Master cache.

### Round 2 — Cache Hit

The same dynamically discovered requests are sent again without clearing the cache. During this round:

- The Master checks its Memcached instance first.
- All previously retrieved values are returned from the Master cache.
- SQLite and the Slave nodes are not accessed again for cached items.

If a sensor is not returned from cache in the second round, possible causes include an unsuccessful first request, an incorrect sensor type or ID in the input CSV files, cache expiration, a Memcached restart, a manual cache flush, or a failed cache insertion. In strict mode, the script exits with an error if a successful round-2 response is not served from cache. The `--no-strict` option disables this final failure condition while still recording the result.

## Benchmark Results

The measured results were:

| Sensor ID | Sensor Type | Round 1 Source | Round 1 Time (µs) | Round 2 Source | Round 2 Time (µs) |
|---:|---|---|---:|---|---:|
| 101 | temperature | master | 8357 | cache | 182 |
| 102 | humidity | master | 486 | cache | 90 |
| 103 | motion | master | 457 | cache | 205 |
| 104 | temperature | master | 603 | cache | 159 |
| 201 | temperature | slave1 | 9588 | cache | 66 |
| 202 | humidity | slave1 | 1512 | cache | 120 |
| 203 | motion | slave1 | 1789 | cache | 117 |
| 204 | co2 | slave1 | 1755 | cache | 139 |
| 301 | temperature | slave2 | 9954 | cache | 109 |
| 302 | humidity | slave2 | 2348 | cache | 108 |
| 303 | motion | slave2 | 2814 | cache | 73 |
| 304 | smoke | slave2 | 2590 | cache | 217 |

## Result Analysis

The benchmark confirms that the caching layer works correctly.

In the first round, the cache was empty. The Master therefore accessed its local SQLite database or forwarded the request to a Slave node. Requests involving a Slave had higher response times because they included network communication, HTTP processing, a Slave cache lookup, and potentially a local SQLite read.

In the second round, every request was returned from the Master cache. The response source for all 12 sensors was `cache`, and the response times were substantially lower.

For example:

| Sensor | First Request | Second Request |
|---|---:|---:|
| 101 | 8357 µs | 182 µs |
| 201 | 9588 µs | 66 µs |
| 301 | 9954 µs | 109 µs |

The first request for sensor `201` required a distributed lookup through Slave1 and took `9588 µs`. The second request was served directly from the Master cache and took only `66 µs`.

Similarly, the first request for sensor `301` required communication with Slave2 and took `9954 µs`, while the cached request took `109 µs`.

These results show that Memcached reduces repeated-read latency, lowers the number of SQLite reads, and prevents unnecessary communication with remote nodes. The improvement is especially clear for sensors stored on the Slave nodes because a Master cache hit removes both database access and network communication.

The unusually high time of the first request for some sensors may also include one-time costs such as TCP connection establishment, filesystem page loading, and cold network or process state. Therefore, the benchmark is mainly intended to demonstrate the functional difference between cache-miss and cache-hit paths rather than provide a hardware-independent performance measurement.

## Logs and Utility Scripts

Application logs are stored in:

```text
logs/master.log
logs/slave1.log
logs/slave2.log
```

Logs created by background execution can be displayed with:

```bash
bash scripts/show_logs.sh
```

A single node log can be displayed by passing its name:

```bash
bash scripts/show_logs.sh master
```

Functional request tests can be performed on the Master VM with:

```bash
bash scripts/test_requests.sh
```

The test script chooses suitable sensors dynamically from the CSV files, verifies the original Master, Slave1, and Slave2 lookup paths, repeats each request to verify the Master cache, and checks the expected HTTP errors.

The complete build and execution workflow for a selected node is provided by:

```bash
bash scripts/build_and_run.sh master
bash scripts/build_and_run.sh slave1
bash scripts/build_and_run.sh slave2
```

The local cache of a selected node can be cleared with `flush_cache.sh`, background nodes can be stopped with `stop_node.sh`, and generated build, PID, log, benchmark, and editor-temporary files can be removed with:

```bash
bash scripts/clean_generated_files.sh
```

## Summary

Section 2 successfully adds a two-layer read architecture to every node:

```text
Memcached
    |
    v
SQLite
```

The Master cache is checked before the Master database and before forwarding requests to Slave1 or Slave2. On a cache miss, data is retrieved from the appropriate SQLite database and stored in the cache. On a cache hit, data is returned directly from memory.

The two-round benchmark confirms that all 12 sensors are retrieved from their original databases during the first round and from the Master cache during the second round. This design reduces response time, lowers SQLite read load, and avoids repeated network communication with the Slave nodes.