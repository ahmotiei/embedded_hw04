# Final Video Recording Commands

This file contains only the commands and outputs that should appear in the final video; run each server command in the specified terminal and leave foreground processes open while the related tests are executed.

---

# Section 1 — Distributed Database

## Test 1 — Start Slave1

**Run on:** Slave1 VM

**Explanation:** This command initializes the Slave1 database, compiles the program, and starts the server on port `9001`; leave this terminal open.

```bash
cd /home/amir/embedded_hw04/01
./slave1/slave1_init_db.sh ./slave1/slave1.db ./data/slave1_sensors.csv
make -C slave1 clean
make -C slave1
cd slave1
stdbuf -oL -eL ./slave1 config
```

**Expected output:**

```text
The database is initialized and Slave1 remains running on port 9001 without an error.
```

---

## Test 2 — Start Slave2

**Run on:** Slave2 VM

**Explanation:** This command initializes the Slave2 database, compiles the program, and starts the server on port `9002`; leave this terminal open.

```bash
cd /home/amir/embedded_hw04/01
./slave2/slave2_init_db.sh ./slave2/slave2.db ./data/slave2_sensors.csv
make -C slave2 clean
make -C slave2
cd slave2
stdbuf -oL -eL ./slave2 config
```

**Expected output:**

```text
The database is initialized and Slave2 remains running on port 9002 without an error.
```

---

## Test 3 — Start the Master

**Run on:** Master VM

**Explanation:** This command initializes the Master database, compiles the program, and starts the Master API on port `8000`; leave this terminal open.

```bash
cd /home/amir/embedded_hw04/01
./master/master_init_db.sh ./master/master.db ./data/master_sensors.csv
make -C master clean
make -C master
cd master
stdbuf -oL -eL ./master config
```

**Expected output:**

```text
The database is initialized and the Master remains running on port 8000 without an error.
```

---

## Test 4 — Run the Automated Distributed Test

**Run on:** Host

**Explanation:** This script tests successful Master and Slave lookups together with the required HTTP error cases.

```bash
cd /home/amir-hossein-motiei/embedded/embedded_hw04/01
MASTER_HOST=192.168.122.22 ./scripts/test_requests.sh
```

**Expected output:**

```text
Passed: 6
Failed: 0
All tests passed successfully.
```

---

## Test 5 — Display a Master Response

**Run on:** Host

**Explanation:** This request displays a sensor value returned directly from the Master database.

```bash
curl -s "http://192.168.122.22:8000/api/sensor?id=101&type=temperature" | python3 -m json.tool
```

**Expected output:**

```json
{
    "source": "master",
    "data": {
        "sensor_id": "101"
    }
}
```

---

## Test 6 — Display a Slave1 Response Through the Master

**Run on:** Host

**Explanation:** This request shows that the Master forwards the lookup to Slave1 and returns the result to the operator.

```bash
curl -s "http://192.168.122.22:8000/api/sensor?id=204&type=co2" | python3 -m json.tool
```

**Expected output:**

```json
{
    "source": "slave1",
    "data": {
        "sensor_id": "204"
    }
}
```

---

## Test 7 — Display a Slave2 Response Through the Master

**Run on:** Host

**Explanation:** This request shows that the Master continues the distributed lookup and returns the result obtained from Slave2.

```bash
curl -s "http://192.168.122.22:8000/api/sensor?id=304&type=smoke" | python3 -m json.tool
```

**Expected output:**

```json
{
    "source": "slave2",
    "data": {
        "sensor_id": "304"
    }
}
```

---

# Section 2 — SQLite and Memcached Cache Layer

## Test 1 — Start Slave1 with the Build-and-Run Script

**Run on:** Slave1 VM

**Explanation:** This script initializes Slave1, checks Memcached, compiles the program, flushes the local cache, and starts the node; leave this terminal open.

```bash
cd /home/amir/embedded_hw04/02
bash scripts/build_and_run.sh slave1
```

**Expected output:**

```text
Slave running on port 9001, database: slave1.db, cache: enabled
```

---

## Test 2 — Start Slave2 with the Build-and-Run Script

**Run on:** Slave2 VM

**Explanation:** This script initializes Slave2, checks Memcached, compiles the program, flushes the local cache, and starts the node; leave this terminal open.

```bash
cd /home/amir/embedded_hw04/02
bash scripts/build_and_run.sh slave2
```

**Expected output:**

```text
Slave running on port 9002, database: slave2.db, cache: enabled
```

---

## Test 3 — Start the Master with the Build-and-Run Script

**Run on:** Master VM

**Explanation:** This script initializes the Master, checks Memcached, compiles the program, flushes the local cache, and starts the API; leave this terminal open.

```bash
cd /home/amir/embedded_hw04/02
bash scripts/build_and_run.sh master
```

**Expected output:**

```text
Master running on port 8000, database: master.db, cache: enabled
Master Memcached: 127.0.0.1:11211, TTL: 300 seconds
```

---

## Test 4 — Compare Cold-Cache and Warm-Cache Response Times

**Run on:** Master VM in a new terminal

**Explanation:** This benchmark reads all sensors twice and demonstrates that the second round is served from cache with a lower response time.

```bash
cd /home/amir/embedded_hw04/02
bash scripts/cache_speed_test.sh
```

**Expected output:**

```text
Round 1:
Requests: 12
Cache hits: 0
Sources: master, slave1, slave2

Round 2:
Requests: 12
Cache hits: 12
Source: cache

Round 2 mean server response time is lower than Round 1.
Server-time reduction: <measured percentage>
Server-time speedup: <measured ratio>x
Cache benchmark completed successfully.
```

---

# Section 3 — MQTT Integration

## Test 1 — Start the Mosquitto Broker

**Run on:** Master VM in the Broker terminal

**Explanation:** This command starts the project Broker in verbose mode on port `1883`; leave this terminal open.

```bash
cd /home/amir/embedded_hw04/03
sudo systemctl stop mosquitto 2>/dev/null || true
pkill -x mosquitto 2>/dev/null || true
mosquitto -c mqtt/mosquitto.conf -v
```

**Expected output:**

```text
Opening ipv4 listen socket on port 1883
mosquitto version <version> running
```

---

## Test 2 — Start Slave1 with MQTT Support

**Run on:** Slave1 VM

**Explanation:** This script initializes, compiles, validates the required services, and starts Slave1; leave this terminal open.

```bash
cd /home/amir/embedded_hw04/03
./scripts/build_and_run.sh slave1
```

**Expected output:**

```text
Slave1 starts successfully on port 9001 with SQLite and Memcached available.
```

---

## Test 3 — Start Slave2 with MQTT Support

**Run on:** Slave2 VM

**Explanation:** This script initializes, compiles, validates the required services, and starts Slave2; leave this terminal open.

```bash
cd /home/amir/embedded_hw04/03
./scripts/build_and_run.sh slave2
```

**Expected output:**

```text
Slave2 starts successfully on port 9002 with SQLite and Memcached available.
```

---

## Test 4 — Start the MQTT-Enabled Master

**Run on:** Master VM in a new terminal

**Explanation:** This script initializes and compiles the Master, verifies the Broker, and starts its HTTP and MQTT services; leave this terminal open.

```bash
cd /home/amir/embedded_hw04/03
./scripts/build_and_run.sh master
```

**Expected output:**

```text
MQTT version: 3.1.1
QoS: 1
Subscribed topic: hotel/sensors/request
HTTP API listening on port 8000
```

---

## Test 5 — Run the MQTT Functional Test

**Run on:** Master VM in a new terminal

**Explanation:** This script verifies Master, Slave1, Slave2, cache, and unknown-sensor MQTT responses.

```bash
cd /home/amir/embedded_hw04/03
./scripts/mqtt_test.sh
```

**Expected output:**

```text
Master database lookup      PASSED
Slave1 distributed lookup   PASSED
Slave2 distributed lookup   PASSED
Repeated request uses cache PASSED
Unknown sensor              PASSED
Passed: 5
Failed: 0
All MQTT functional tests passed.
```

---

## Test 6 — Subscribe to MQTT Responses

**Run on:** Host in the Subscriber terminal

**Explanation:** This subscriber displays every sensor response received from the Broker; leave this terminal open for the next test.

```bash
mosquitto_sub \
  -h 192.168.122.22 \
  -p 1883 \
  -V mqttv311 \
  -q 1 \
  -t "hotel/sensors/response/#" \
  -v
```

**Expected output:**

```text
The terminal waits for messages on hotel/sensors/response/#.
```

---

## Test 7 — Publish and Receive an MQTT Sensor Request

**Run on:** Host in a second terminal

**Explanation:** This command publishes a QoS 1 request and the Subscriber terminal displays the matching response topic and JSON payload.

```bash
mosquitto_pub \
  -h 192.168.122.22 \
  -p 1883 \
  -V mqttv311 \
  -q 1 \
  -t "hotel/sensors/request" \
  -m '{"sensor_type":"temperature","sensor_id":"101"}'
```

**Expected output in the Subscriber terminal:**

```text
hotel/sensors/response/101 {"success":true,"sensor_id":"101","source":"master",...}
```

---

## Test 8 — Compare MQTT Lookup and Cache Performance

**Run on:** Master VM in a new terminal

**Explanation:** This benchmark sends all sensor requests in two rounds and compares the normal distributed path with the Master cache.

```bash
cd /home/amir/embedded_hw04/03
./scripts/mqtt_speed_test.sh
```

**Expected output:**

```text
Round 1: source=master, slave1, or slave2
Round 2: source=cache for all 12 sensors

Average service time, Round 1: <measured value> us
Average service time, Round 2: <measured value> us
Service-time reduction: <measured percentage>
Service-time speedup: <measured ratio>x
Benchmark completed successfully.
```

---

# Section 4 — Reading Sensor Data with SNMP

## Test 1 — Start the Master SNMP Agent

**Run on:** Master VM

**Explanation:** This script compiles the custom handler, generates the Master configuration, and starts the SNMP agent on UDP port `1161`.

```bash
cd /home/amir/embedded_hw04/04
./scripts/build_and_run.sh config/master.env
```

**Expected output:**

```text
SNMP agent started.
Node:       master
Listen:     udp:1161
Base OID:   .1.3.6.1.4.1.8072.9999.4
```

---

## Test 2 — Start the Slave1 SNMP Agent

**Run on:** Slave1 VM

**Explanation:** This script compiles the custom handler, generates the Slave1 configuration, and starts its SNMP agent on UDP port `1161`.

```bash
cd /home/amir/embedded_hw04/04
./scripts/build_and_run.sh config/slave1.env
```

**Expected output:**

```text
SNMP agent started.
Node:       slave1
Listen:     udp:1161
Base OID:   .1.3.6.1.4.1.8072.9999.4
```

---

## Test 3 — Start the Slave2 SNMP Agent

**Run on:** Slave2 VM

**Explanation:** This script compiles the custom handler, generates the Slave2 configuration, and starts its SNMP agent on UDP port `1161`.

```bash
cd /home/amir/embedded_hw04/04
./scripts/build_and_run.sh config/slave2.env
```

**Expected output:**

```text
SNMP agent started.
Node:       slave2
Listen:     udp:1161
Base OID:   .1.3.6.1.4.1.8072.9999.4
```

---

## Test 4 — Read All Sensors from All Nodes

**Run on:** Master VM

**Explanation:** This script performs SNMP walks against the Master and both Slave agents and displays every sensor field required by the assignment.

```bash
cd /home/amir/embedded_hw04/04
./scripts/read_all_sensors.sh config/nodes.env
```

**Expected output:**

```text
Node: master
INTEGER: 4
Sensor IDs: 101, 102, 103, 104

Node: slave1
INTEGER: 4
Sensor IDs: 201, 202, 203, 204

Node: slave2
INTEGER: 4
Sensor IDs: 301, 302, 303, 304

Each sensor displays its name, description, latest value, unit, timestamp, type, and node.
```

---

# Section 5 — Sensor Log API with Mongoose

## Test 1 — Build and Start the Sensor Log API

**Run on:** Master VM

**Explanation:** This script compiles the Mongoose API and starts it on port `8005`; leave this terminal open.

```bash
cd /home/amir/embedded_hw04/05
./scripts/build_and_run.sh
```

**Expected output:**

```text
Sensor log API is listening on http://0.0.0.0:8005
SQLite source: /home/amir/embedded_hw04/01/master/master.db
```

---

## Test 2 — Run the Sensor Log API Test

**Run on:** Master VM in a new terminal

**Explanation:** This script checks the health endpoint and retrieves all stored values of sensor `101` on the requested date.

```bash
cd /home/amir/embedded_hw04/05
./scripts/test_api.sh
```

**Expected output:**

```json
{
  "status": "ok"
}
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

---

# Section 6 — Sensor Alert Daemon

## Test 1 — Build and Run One Real Monitoring Cycle

**Run on:** Master VM

**Explanation:** This command resets the demonstration alert database, compiles the daemon, reads the real Master database, and performs one monitoring cycle.

```bash
cd /home/amir/embedded_hw04/06
rm -f data/alerts.db
./scripts/build_and_run.sh config/daemon.env --once
```

**Expected output:**

```text
sensor alert daemon started
STALE_DATA sensor_id=101 value=24.8
STALE_DATA sensor_id=102 value=45
STALE_DATA sensor_id=103 value=1
STALE_DATA sensor_id=104 value=23.9
cycle complete: sensors=4 inserted=4 resolved=0
sensor alert daemon stopped
```

---

## Test 2 — Run the Automated Alert Test

**Run on:** Master VM

**Explanation:** This test creates abnormal readings, runs the daemon, and verifies three alert types together with duplicate prevention.

```bash
cd /home/amir/embedded_hw04/06
make test
```

**Expected output:**

```text
sensor_id  sensor_name          alert_type        sensor_value  status
---------  -------------------  ----------------  ------------  ------
101        temperature          TEMPERATURE_HIGH  35.2          OPEN
201        humidity             HUMIDITY_LOW      18.5          OPEN
301        temperature-invalid  INVALID_VALUE     not-a-number  OPEN
PASS: daemon alert test succeeded.
```

---

## Test 3 — Display the Alerts Stored in SQLite

**Run on:** Master VM

**Explanation:** This query displays the alert records and all fields required by the assignment directly from the test alert database.

```bash
cd /home/amir/embedded_hw04/06
sqlite3 -header -column tests/alerts.db \
  "SELECT id, sensor_id, sensor_name, sensor_value, alert_type, created_at, status FROM alerts ORDER BY id;"
```

**Expected output:**

```text
id  sensor_id  sensor_name          sensor_value  alert_type        created_at           status
--  ---------  -------------------  ------------  ----------------  -------------------  ------
1   101        temperature          35.2          TEMPERATURE_HIGH  <timestamp>          OPEN
2   201        humidity             18.5          HUMIDITY_LOW      <timestamp>          OPEN
3   301        temperature-invalid  not-a-number  INVALID_VALUE     <timestamp>          OPEN
```
