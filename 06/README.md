# Section 06 — Sensor Alert Daemon

This section implements a configurable background monitoring service for hotel sensor data. The daemon periodically reads the latest value of every active sensor from one or more SQLite databases, evaluates the values against configurable alert rules, and records alert events in a separate SQLite alert database.

The implementation is written in **C++17**, uses **SQLite**, is compiled with a **Makefile**, can be started with the provided **Bash scripts**, and can run continuously as a managed **systemd service**.

---

## 1. Requirements Covered

The implementation satisfies the Section 06 requirements:

- A C++ daemon monitors sensor values.
- Monitoring is performed periodically at a configurable interval.
- Alert conditions are defined in a configuration file.
- Alerts are stored in a SQLite database.
- Every alert stores at least:
  - sensor identifier;
  - sensor name;
  - sensor value;
  - alert type;
  - alert creation time;
  - alert status.
- A systemd service template is included.
- A Makefile is provided.
- Bash scripts are provided for building, running, testing, initializing the database, and installing the service.
- This README explains compilation, installation, execution, stopping, service status, logs, alert inspection, and alert conditions.

---

## 2. Project Structure

```text
06/
├── config/
│   ├── alert_rules.conf
│   ├── daemon.env
│   └── daemon.env.example
├── daemon/
│   └── alert_daemon.cpp
├── Makefile
├── scripts/
│   ├── build_and_run.sh
│   ├── create_test_db.sh
│   ├── init_alert_db.sh
│   ├── install_service.sh
│   ├── run_daemon.sh
│   └── test_daemon.sh
├── service/
│   └── sensor-alert.service.template
└── tests/
```

The executable is generated at:

```text
daemon/sensor_alert_daemon
```

The default alert database is generated at:

```text
data/alerts.db
```

---

## 3. Architecture and Data Flow

```text
One or more sensor SQLite databases
              |
              | Read-only query for the latest reading
              | of every active sensor
              v
      Sensor Alert Daemon
              |
              | Evaluate configurable rules
              |
       +------+------+
       |             |
  No alert       Alert detected
       |             |
       |             v
       |      Insert OPEN alert
       |      into alerts.db
       |             |
       +-------> Resolve old OPEN alerts
                 when their condition is no longer active
```

For each monitoring cycle, the daemon performs the following operations:

1. Opens each configured source database in read-only mode.
2. Reads the latest available reading of every active sensor.
3. Calculates the age of each reading.
4. Validates the recorded timestamp and sensor value.
5. Classifies temperature and humidity sensors using their `sensor_type` and `sensor_name` fields.
6. Evaluates all enabled alert conditions.
7. Inserts newly detected alerts with status `OPEN`.
8. Changes previously open alerts to `RESOLVED` when the latest sensor reading no longer satisfies that alert condition.
9. Sleeps for the configured interval and starts the next cycle.

The daemon can also run one cycle only by using the `--once` option. This mode is useful for testing and automated validation.

---

## 4. Dependencies

The tested environment is Ubuntu 22.04. Install the required packages on the VM that will run the daemon:

```bash
sudo apt update
sudo apt install -y build-essential g++ make sqlite3 libsqlite3-dev
```

Check the installed tools:

```bash
g++ --version
make --version
sqlite3 --version
dpkg -s libsqlite3-dev
systemctl --version
```

The daemon does not require Mongoose, Memcached, Mosquitto, or Net-SNMP.

---

## 5. Source Database Requirements

Every configured source SQLite database must contain the following tables.

### 5.1 `sensors` table

The daemon uses these columns:

```sql
CREATE TABLE sensors (
    sensor_id TEXT PRIMARY KEY,
    sensor_type TEXT NOT NULL,
    sensor_name TEXT NOT NULL,
    location TEXT,
    unit TEXT,
    node_name TEXT,
    is_active INTEGER DEFAULT 1
);
```

### 5.2 `sensor_readings` table

The daemon uses these columns:

```sql
CREATE TABLE sensor_readings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    sensor_id TEXT NOT NULL,
    value TEXT NOT NULL,
    recorded_at TEXT NOT NULL
);
```

Only sensors for which `is_active` is `1` are monitored. For every active sensor, only its latest reading is evaluated. The latest record is selected by `recorded_at`, with `id` used as a tie-breaker.

The source databases are opened in **read-only mode**. The daemon does not modify sensor data.

---

## 6. Alert Database Schema

The alert database is initialized by `scripts/init_alert_db.sh`. The daemon can also create the schema automatically if the database does not yet exist.

```sql
CREATE TABLE alerts (
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
    UNIQUE(
        sensor_id,
        sensor_name,
        alert_type,
        source_recorded_at,
        source_db
    )
);
```

The first seven columns cover the required project fields. The additional fields improve traceability:

- `source_recorded_at`: timestamp of the sensor reading that caused the alert;
- `source_db`: source database from which the reading was obtained;
- `details`: human-readable explanation of the rule that generated the alert.

The database uses SQLite WAL mode and includes indexes for status/time and sensor-based queries.

### Alert status values

```text
OPEN
RESOLVED
```

A new alert is inserted as `OPEN`. When a later reading no longer triggers that alert type, the daemon changes the existing open alert to `RESOLVED`.

### Duplicate prevention

The `UNIQUE` constraint prevents repeated monitoring cycles from inserting duplicate alerts for the same sensor reading, alert type, and source database.

---

## 7. Runtime Configuration

The runtime configuration is stored in:

```text
config/daemon.env
```

Default tested configuration:

```bash
SOURCE_DATABASES=/home/amir/master/master.db
ALERT_DATABASE=/home/amir/06/data/alerts.db
RULES_FILE=/home/amir/06/config/alert_rules.conf
CHECK_INTERVAL_SECONDS=5
```

### Configuration variables

| Variable | Description |
|---|---|
| `SOURCE_DATABASES` | One or more source SQLite database paths |
| `ALERT_DATABASE` | Destination SQLite database used for alerts |
| `RULES_FILE` | Alert threshold configuration file |
| `CHECK_INTERVAL_SECONDS` | Delay between monitoring cycles |
| `SERVICE_USER` | Optional systemd user; defaults to the user running the installer |

### Multiple source databases

Multiple source databases can be configured by separating paths with a colon:

```bash
SOURCE_DATABASES=/path/master.db:/path/slave1.db:/path/slave2.db
```

The launcher converts each path into a separate `--source-db` argument. If one source database cannot be read, the daemon logs an error and continues with the remaining readable sources. If none of the configured source databases can be read, the cycle fails.

### Create the local configuration

When deploying to another machine, copy and edit the example file:

```bash
cp config/daemon.env.example config/daemon.env
nano config/daemon.env
```

IP addresses, database paths, rule paths, and monitoring intervals are not hardcoded in the C++ source code.

---

## 8. Alert Rules

Alert thresholds are stored in:

```text
config/alert_rules.conf
```

Default rules:

```bash
TEMPERATURE_MAX=30.0
TEMPERATURE_MIN_VALID=-50.0
TEMPERATURE_MAX_VALID=100.0

HUMIDITY_MIN=30.0
HUMIDITY_MAX=70.0
HUMIDITY_MIN_VALID=0.0
HUMIDITY_MAX_VALID=100.0

STALE_AFTER_SECONDS=600
CHECK_INTERVAL_SECONDS=5
```

### 8.1 `TEMPERATURE_HIGH`

Generated when a valid temperature value is greater than `TEMPERATURE_MAX`.

Example:

```text
Temperature = 35.2
TEMPERATURE_MAX = 30.0
Result: TEMPERATURE_HIGH
```

### 8.2 `HUMIDITY_LOW`

Generated when a valid humidity value is lower than `HUMIDITY_MIN`.

Example:

```text
Humidity = 18.5
HUMIDITY_MIN = 30.0
Result: HUMIDITY_LOW
```

### 8.3 `HUMIDITY_HIGH`

Generated when a valid humidity value is greater than `HUMIDITY_MAX`.

Example:

```text
Humidity = 85.0
HUMIDITY_MAX = 70.0
Result: HUMIDITY_HIGH
```

### 8.4 `STALE_DATA`

Generated when the latest sensor reading is older than `STALE_AFTER_SECONDS`.

Example:

```text
Latest reading age = 900 seconds
STALE_AFTER_SECONDS = 600
Result: STALE_DATA
```

This condition represents a sensor that has not produced a recent reading within the configured time window.

### 8.5 `INVALID_VALUE`

Generated in any of these situations:

- the sensor value is empty, non-numeric, infinite, or not a finite number;
- a temperature is outside `TEMPERATURE_MIN_VALID` to `TEMPERATURE_MAX_VALID`;
- a humidity value is outside `HUMIDITY_MIN_VALID` to `HUMIDITY_MAX_VALID`;
- `recorded_at` is not a valid SQLite date/time value.

A reading may generate more than one alert. For example, an old temperature reading above the maximum can generate both `STALE_DATA` and `TEMPERATURE_HIGH`.

### Sensor classification

A sensor is treated as temperature-related when `sensor_type` or `sensor_name` contains `temperature` or `temp`. It is treated as humidity-related when either field contains `humidity` or `humid`. Matching is case-insensitive.

---

## 9. Deployment from the Host to the Master VM

The project files are edited on the Host and copied to the Master VM.

### On the Host

```bash
cd /home/amir-hossein-motiei/embedded/embedded_hw04
scp -r 06 amir@<MASTER_IP>:/home/amir/
```

Example used in the test environment:

```bash
scp -r 06 amir@192.168.122.22:/home/amir/
```

### On the Master VM

```bash
cd /home/amir/06
find . -maxdepth 3 -type f | sort
```

The service and daemon are executed on the Master VM. No direct action is required on Slave1 or Slave2 when only the Master database is configured.

---

## 10. Compilation with Makefile

### On the Master VM

```bash
cd /home/amir/06
make clean
make all
```

Expected compilation command:

```text
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    daemon/alert_daemon.cpp -lsqlite3 -pthread \
    -o daemon/sensor_alert_daemon
```

Verify the executable:

```bash
ls -lh daemon/sensor_alert_daemon
file daemon/sensor_alert_daemon
ldd daemon/sensor_alert_daemon | grep -E 'sqlite|libstdc|not found'
```

### Makefile targets

| Target | Purpose |
|---|---|
| `make all` | Compiles the daemon |
| `make clean` | Removes the executable and standard test databases |
| `make test` | Builds the daemon and runs the automated alert test |

---

## 11. Initialize the Alert Database

Initialize the default alert database using the configured path:

```bash
cd /home/amir/06
source config/daemon.env
./scripts/init_alert_db.sh "$ALERT_DATABASE"
```

Or specify a path directly:

```bash
./scripts/init_alert_db.sh /home/amir/06/data/alerts.db
```

Expected output:

```text
Alert database initialized: /home/amir/06/data/alerts.db
```

The standalone `wal` line printed by SQLite is the result of enabling WAL journal mode and is not an error.

---

## 12. Run One Monitoring Cycle

The easiest complete one-cycle test compiles the project, initializes the alert database, loads the configuration, and runs the daemon once:

```bash
cd /home/amir/06
./scripts/build_and_run.sh config/daemon.env --once
```

Expected startup and shutdown sequence:

```text
[INFO] sensor alert daemon started
[INFO] alert database: /home/amir/06/data/alerts.db
[INFO] check interval: 5 seconds
[INFO] cycle complete: sensors=<count> inserted=<count> resolved=<count>
[INFO] sensor alert daemon stopped
```

A zero exit code indicates successful completion:

```bash
echo $?
```

---

## 13. Run Continuous Periodic Monitoring

After the binary has been compiled, run it continuously with:

```bash
cd /home/amir/06
./scripts/run_daemon.sh config/daemon.env
```

With `CHECK_INTERVAL_SECONDS=5`, one cycle is executed every five seconds.

Stop a foreground run with:

```text
Ctrl+C
```

The daemon handles both `SIGINT` and `SIGTERM`, closes the alert database, and exits cleanly.

Expected shutdown log:

```text
[INFO] sensor alert daemon stopped
```

---

## 14. Direct Command-Line Execution

The binary can be run without the wrapper scripts:

```bash
./daemon/sensor_alert_daemon \
  --source-db /home/amir/master/master.db \
  --alert-db /home/amir/06/data/alerts.db \
  --rules /home/amir/06/config/alert_rules.conf \
  --interval 5
```

Run one cycle only:

```bash
./daemon/sensor_alert_daemon \
  --source-db /home/amir/master/master.db \
  --alert-db /home/amir/06/data/alerts.db \
  --rules /home/amir/06/config/alert_rules.conf \
  --once
```

Use more than one source database by repeating `--source-db`:

```bash
./daemon/sensor_alert_daemon \
  --source-db /path/master.db \
  --source-db /path/slave1.db \
  --source-db /path/slave2.db \
  --alert-db /path/alerts.db \
  --rules /path/alert_rules.conf \
  --interval 5
```

Command-line syntax:

```text
sensor_alert_daemon
  --source-db <sqlite-path>
  [--source-db <sqlite-path> ...]
  --alert-db <sqlite-path>
  --rules <rules-file>
  [--interval <seconds>]
  [--once]
```

The `--interval` value must be between 1 and 86400 seconds.

---

## 15. Automated Test

The automated test creates an isolated source database with three deliberately abnormal readings:

```text
Temperature 35.2       -> TEMPERATURE_HIGH
Humidity 18.5          -> HUMIDITY_LOW
Value not-a-number     -> INVALID_VALUE
```

Run the test:

```bash
cd /home/amir/06
make test
```

Expected final output:

```text
sensor_id  sensor_name          alert_type        sensor_value  status
---------  -------------------  ----------------  ------------  ------
101        temperature          TEMPERATURE_HIGH  35.2          OPEN
201        humidity             HUMIDITY_LOW      18.5          OPEN
301        temperature-invalid  INVALID_VALUE     not-a-number  OPEN
PASS: daemon alert test succeeded.
```

The test executes the daemon twice against the same source readings. The second cycle verifies that duplicate alerts are not inserted.

Test files:

```text
tests/source.db
tests/alerts.db
```

Inspect the test alerts:

```bash
sqlite3 -header -column tests/alerts.db \
  "SELECT sensor_id, sensor_name, alert_type, sensor_value, status FROM alerts ORDER BY id;"
```

Check duplicate prevention:

```bash
sqlite3 -header -column tests/alerts.db "
SELECT
    COUNT(*) AS total_alerts,
    COUNT(
        DISTINCT sensor_id || '|' ||
        sensor_name || '|' ||
        alert_type || '|' ||
        source_recorded_at || '|' ||
        source_db
    ) AS unique_alerts
FROM alerts;
"
```

For the standard automated test, both values must be `3`.

---

## 16. Test Alert Resolution

This optional test demonstrates the transition from `OPEN` to `RESOLVED`.

Create an isolated source database:

```bash
cd /home/amir/06
rm -f tests/resolution_source.db tests/resolution_alerts.db
./scripts/create_test_db.sh tests/resolution_source.db
```

Create the initial open alerts:

```bash
./daemon/sensor_alert_daemon \
  --source-db tests/resolution_source.db \
  --alert-db tests/resolution_alerts.db \
  --rules config/alert_rules.conf \
  --once
```

Insert new normal readings:

```bash
sqlite3 tests/resolution_source.db "
INSERT INTO sensor_readings(sensor_id, value, recorded_at)
VALUES
    ('101', '25.0', datetime('now')),
    ('201', '45.0', datetime('now')),
    ('301', '22.0', datetime('now'));
"
```

Run another cycle:

```bash
./daemon/sensor_alert_daemon \
  --source-db tests/resolution_source.db \
  --alert-db tests/resolution_alerts.db \
  --rules config/alert_rules.conf \
  --once
```

Expected cycle result:

```text
inserted=0 resolved=3
```

Inspect alert status:

```bash
sqlite3 -header -column tests/resolution_alerts.db \
  "SELECT id, sensor_id, alert_type, sensor_value, status FROM alerts ORDER BY id;"
```

Expected status:

```text
RESOLVED
```

If a later reading violates a rule again, a new `OPEN` alert is inserted because it has a new `source_recorded_at` value.

---

## 17. Install the systemd Service

The installer compiles the daemon, initializes the alert database, generates the final service file from the template, installs it, reloads systemd, enables the service at boot, and starts it immediately.

### On the Master VM

```bash
cd /home/amir/06
./scripts/install_service.sh config/daemon.env
```

The command requires `sudo` permission.

Generated project service file:

```text
service/sensor-alert.service
```

Installed service file:

```text
/etc/systemd/system/sensor-alert.service
```

The generated service uses the current project path and current service user. In the tested environment, the important lines are:

```ini
[Service]
Type=simple
User=amir
WorkingDirectory=/home/amir/06
ExecStart=/home/amir/06/scripts/run_daemon.sh config/daemon.env
Restart=on-failure
RestartSec=3
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=read-only
ReadWritePaths=/home/amir/06/data
```

The relative configuration path in `ExecStart` is valid because `WorkingDirectory` is `/home/amir/06`.

---

## 18. Start the Daemon Service

```bash
sudo systemctl start sensor-alert.service
```

Enable startup after reboot:

```bash
sudo systemctl enable sensor-alert.service
```

Start and enable in one command:

```bash
sudo systemctl enable --now sensor-alert.service
```

---

## 19. Stop the Daemon Service

```bash
sudo systemctl stop sensor-alert.service
```

Check that it stopped:

```bash
systemctl is-active sensor-alert.service || true
```

Expected result:

```text
inactive
```

The service sends `SIGTERM`, which the daemon handles gracefully.

---

## 20. Restart the Daemon Service

```bash
sudo systemctl restart sensor-alert.service
```

Verify that the service is running:

```bash
systemctl is-active sensor-alert.service
```

Expected result:

```text
active
```

Retrieve the process ID from systemd:

```bash
systemctl show --property=MainPID --value sensor-alert.service
```

Do not rely on `pgrep -x sensor_alert_daemon`, because Linux may truncate the process communication name to 15 characters. Use the systemd `MainPID` value or this command instead:

```bash
pgrep -af '/home/amir/06/daemon/sensor_alert_daemon'
```

---

## 21. Check Service Status

Quick checks:

```bash
systemctl is-enabled sensor-alert.service
systemctl is-active sensor-alert.service
```

Expected results:

```text
enabled
active
```

Full status:

```bash
sudo systemctl --no-pager --full status sensor-alert.service
```

Detailed machine-readable properties:

```bash
systemctl show sensor-alert.service \
  --property=LoadState \
  --property=ActiveState \
  --property=SubState \
  --property=MainPID \
  --property=User \
  --property=ExecMainStatus \
  --no-pager
```

Expected values while running:

```text
LoadState=loaded
ActiveState=active
SubState=running
MainPID=<positive process ID>
User=amir
ExecMainStatus=0
```

---

## 22. View systemd Logs

Display recent service logs:

```bash
sudo journalctl -u sensor-alert.service -n 50 --no-pager
```

Display logs with ISO timestamps:

```bash
sudo journalctl \
  -u sensor-alert.service \
  -n 50 \
  --no-pager \
  --output=short-iso
```

Follow logs live:

```bash
sudo journalctl -u sensor-alert.service -f
```

Display logs since a specific time:

```bash
sudo journalctl \
  -u sensor-alert.service \
  --since "10 minutes ago" \
  --no-pager
```

Typical log messages:

```text
[INFO] sensor alert daemon started
[INFO] alert database: /home/amir/06/data/alerts.db
[INFO] check interval: 5 seconds
[ALERT] STALE_DATA sensor_id=101 value=24.8 source=/home/amir/master/master.db
[INFO] cycle complete: sensors=4 inserted=1 resolved=0
[INFO] sensor alert daemon stopped
```

---

## 23. Inspect Registered Alerts

Show every stored alert:

```bash
sqlite3 -header -column /home/amir/06/data/alerts.db "
SELECT
    id,
    sensor_id,
    sensor_name,
    alert_type,
    sensor_value,
    created_at,
    status
FROM alerts
ORDER BY id;
"
```

Show open alerts only:

```bash
sqlite3 -header -column /home/amir/06/data/alerts.db "
SELECT
    id,
    sensor_id,
    sensor_name,
    alert_type,
    sensor_value,
    created_at,
    details
FROM alerts
WHERE status = 'OPEN'
ORDER BY created_at DESC;
"
```

Show resolved alerts only:

```bash
sqlite3 -header -column /home/amir/06/data/alerts.db "
SELECT
    id,
    sensor_id,
    sensor_name,
    alert_type,
    sensor_value,
    created_at
FROM alerts
WHERE status = 'RESOLVED'
ORDER BY created_at DESC;
"
```

Show status totals:

```bash
sqlite3 -header -column /home/amir/06/data/alerts.db "
SELECT status, COUNT(*) AS alert_count
FROM alerts
GROUP BY status
ORDER BY status;
"
```

Show alert explanations and source information:

```bash
sqlite3 -header -column /home/amir/06/data/alerts.db "
SELECT
    sensor_id,
    alert_type,
    sensor_value,
    source_recorded_at,
    source_db,
    details
FROM alerts
ORDER BY id;
"
```

---

## 24. Test with the Real Master Database

The tested source database is:

```text
/home/amir/master/master.db
```

Inspect the latest sensor readings before running the daemon:

```bash
sqlite3 -header -column /home/amir/master/master.db "
SELECT
    s.sensor_id,
    s.sensor_type,
    s.sensor_name,
    r.value,
    r.recorded_at
FROM sensors AS s
JOIN sensor_readings AS r
  ON r.id = (
      SELECT r2.id
      FROM sensor_readings AS r2
      WHERE r2.sensor_id = s.sensor_id
      ORDER BY datetime(r2.recorded_at) DESC, r2.id DESC
      LIMIT 1
  )
WHERE COALESCE(s.is_active, 1) = 1
ORDER BY s.sensor_id;
"
```

Run one real cycle:

```bash
cd /home/amir/06
./scripts/build_and_run.sh config/daemon.env --once
```

If the source readings are older than `STALE_AFTER_SECONDS`, the daemon generates `STALE_DATA` alerts. This is expected behavior, not an error.

Run the same cycle again:

```bash
./scripts/run_daemon.sh config/daemon.env --once
```

For unchanged readings, the next cycle should report:

```text
inserted=0
```

This confirms duplicate prevention.

---

## 25. Service Security Settings

The systemd template applies the following restrictions:

- `User`: runs the daemon as a non-root user;
- `NoNewPrivileges=true`: prevents gaining additional privileges;
- `PrivateTmp=true`: gives the service a private temporary directory;
- `ProtectSystem=strict`: mounts most of the filesystem read-only for the service;
- `ProtectHome=read-only`: prevents write access to home directories;
- `ReadWritePaths=<alert-directory>`: grants write access only to the alert database directory;
- `Restart=on-failure`: restarts the service only after unexpected failure.

The daemon opens sensor databases in read-only mode and writes only to the configured alert database.

For a production deployment, additional improvements can include:

- running under a dedicated service account;
- restricting database and configuration file permissions;
- adding log rotation or structured logging;
- monitoring service failures;
- backing up the alert database;
- defining retention or archival rules for old resolved alerts.

---

## 26. Troubleshooting

### `Configuration file not found`

Create the runtime file from the example:

```bash
cp config/daemon.env.example config/daemon.env
```

Then correct all database and rules paths.

### `SOURCE_DATABASES is required`

Make sure `SOURCE_DATABASES` is defined in `config/daemon.env`.

### `could not open source database`

Check the path and permissions:

```bash
ls -lh /home/amir/master/master.db
sqlite3 /home/amir/master/master.db ".tables"
```

### `no configured source database could be read`

None of the configured source databases was readable. Correct `SOURCE_DATABASES` and verify SQLite permissions.

### No alerts are created

Check:

1. The daemon is reading the intended source database.
2. Sensors have `is_active = 1`.
3. Every sensor has at least one row in `sensor_readings`.
4. Rule thresholds match the intended values.
5. The alert already exists for the same reading and was ignored by duplicate prevention.

### All sensors produce `STALE_DATA`

Compare the current UTC time with `recorded_at`. Increase `STALE_AFTER_SECONDS` or insert recent test readings if old readings are intentional.

### The service fails after enabling `ProtectSystem=strict`

Ensure `ALERT_DATABASE` is inside the directory listed by `ReadWritePaths`. Reinstall the service after changing the configuration:

```bash
./scripts/install_service.sh config/daemon.env
```

### `pgrep -x sensor_alert_daemon` returns no PID

The Linux process name may be truncated. Use:

```bash
systemctl show --property=MainPID --value sensor-alert.service
```

or:

```bash
pgrep -af '/home/amir/06/daemon/sensor_alert_daemon'
```

### Inspect the complete installed service file

```bash
sudo systemctl cat sensor-alert.service --no-pager
```

---

## 27. Disable or Remove the Service

Stop and disable the service:

```bash
sudo systemctl disable --now sensor-alert.service
```

Remove the installed service file:

```bash
sudo rm -f /etc/systemd/system/sensor-alert.service
sudo systemctl daemon-reload
sudo systemctl reset-failed
```

The project files and alert database remain in `/home/amir/06` unless removed manually.

---

## 28. Final Verification Checklist

Before submission, verify all items below:

```bash
cd /home/amir/06

# Required files
find . -maxdepth 3 -type f | sort

# Bash syntax
for script in scripts/*.sh; do
    bash -n "$script" || exit 1
done

# Clean compilation
make clean all

# Automated test
make test

# One real cycle
./scripts/build_and_run.sh config/daemon.env --once

# Alert database
sqlite3 data/alerts.db ".tables"
sqlite3 -header -column data/alerts.db \
  "SELECT sensor_id, sensor_name, alert_type, sensor_value, created_at, status FROM alerts ORDER BY id;"

# systemd status
systemctl is-enabled sensor-alert.service
systemctl is-active sensor-alert.service
sudo systemctl --no-pager --full status sensor-alert.service

# system logs
sudo journalctl -u sensor-alert.service -n 30 --no-pager
```

The final submission for Section 06 must contain:

- C++ daemon source code;
- Makefile;
- systemd service template;
- Bash build and run scripts;
- service installation script;
- alert database initialization script;
- test scripts;
- configuration files;
- this README;
- the Section 06 design report.
