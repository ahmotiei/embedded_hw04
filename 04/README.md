# Section 04 - Reading Sensor Data with SNMP

## 1. Overview

This section exposes the latest sensor information stored in the distributed SQLite databases through **SNMP**. A separate SNMP agent runs on the Master, Slave1, and Slave2 virtual machines. Each agent reads its local database and publishes the data under a custom OID subtree.

The implementation uses:

- Net-SNMP `snmpd` as the SNMP agent.
- SNMP version **2c**.
- The Net-SNMP `pass` directive for custom OIDs.
- A C++ handler named `sensor_snmp_pass`.
- SQLite as the source of sensor metadata and latest readings.
- UDP port **1161** for the project-specific agent.
- Community string **`hotelMonitor`**.
- Base OID **`.1.3.6.1.4.1.8072.9999.4`**.

The custom agent uses port `1161` so it does not conflict with the default system SNMP agent, which normally uses UDP port `161`.

## 2. Project Structure

```text
04/
├── config/
│   ├── master.env
│   ├── slave1.env
│   ├── slave2.env
│   ├── nodes.env
│   └── nodes.env.example
├── generated/
│   └── <node-name>/
│       ├── snmpd.conf
│       ├── snmpd.log
│       ├── snmpd.pid
│       └── persistent/
├── scripts/
│   ├── build_and_run.sh
│   ├── common.sh
│   ├── generate_snmpd_config.sh
│   ├── install_dependencies.sh
│   ├── read_all_sensors.sh
│   ├── start_snmpd.sh
│   ├── stop_snmpd.sh
│   ├── test_pass_handler.sh
│   └── test_snmp.sh
├── snmp/
│   ├── HOTEL-SENSOR-MIB.txt
│   ├── Makefile
│   ├── sensor_snmp_pass.cpp
│   └── snmpd.conf.template
├── test-output/
├── Makefile
└── README.md
```

The `generated/` and `test-output/` directories contain runtime files and test results. They are recreated automatically when required.

## 3. Test Environment

The implementation was tested with the following topology:

| Node | IP address | Project directory | SQLite database |
|---|---|---|---|
| Master | `192.168.122.22` | `/home/amir/04` | `/home/amir/master/master.db` |
| Slave1 | `192.168.122.18` | `/home/amir/04` | `/home/amir/slave1/slave1.db` |
| Slave2 | `192.168.122.190` | `/home/amir/04` | `/home/amir/slave2/slave2.db` |

All three nodes must be connected to the same network. The Master is used as the SNMP manager for the final distributed test.

---

# 1. Installing the SNMP Packages

Run the following commands on **Master, Slave1, and Slave2**.

## 1.1 Automatic installation

From the section directory:

```bash
cd /home/amir/04
chmod +x scripts/*.sh
./scripts/install_dependencies.sh
```

The script installs these packages:

```text
build-essential
libsqlite3-dev
snmp
snmpd
```

The packages provide:

- `snmpd`: the Net-SNMP agent.
- `snmpget`: reads one or more exact OIDs.
- `snmpwalk`: traverses an OID subtree using repeated GETNEXT requests.
- `g++` and `make`: compile the C++ `pass` handler.
- `libsqlite3-dev`: links the handler to SQLite.

## 1.2 Manual installation

The same installation can be performed manually:

```bash
sudo apt update
sudo apt install -y build-essential libsqlite3-dev snmp snmpd
```

The SQLite command-line tool is optional but useful for inspecting the database:

```bash
sudo apt install -y sqlite3
```

## 1.3 Verify the installation

```bash
snmpd --version
g++ --version
make --version
```

To verify that the client commands exist:

```bash
command -v snmpget
command -v snmpwalk
```

---

# 2. Configuring `snmpd`

## 2.1 Node configuration files

Each VM has its own environment file under `config/`:

```text
config/master.env
config/slave1.env
config/slave2.env
```

Each file defines the node name, database path, listening address, community string, allowed network, base OID, location, and contact information.

Example configuration for the Master:

```bash
NODE_NAME=master
DATABASE_PATH=/home/amir/master/master.db
SNMP_LISTEN_ADDRESS=udp:1161
SNMP_COMMUNITY=hotelMonitor
SNMP_ALLOWED_NETWORK=192.168.122.0/24
SNMP_BASE_OID=.1.3.6.1.4.1.8072.9999.4
SNMP_SYS_LOCATION=Hotel_Floor_1_Master
SNMP_SYS_CONTACT=embedded-lab
```

The corresponding database paths used in the tested deployment are:

```text
Master: /home/amir/master/master.db
Slave1: /home/amir/slave1/slave1.db
Slave2: /home/amir/slave2/slave2.db
```

Before running the agent, verify that the database path in each configuration file matches the real database location on that VM:

```bash
ls -lh /home/amir/master/master.db
```

Use the appropriate path on Slave1 and Slave2.

## 2.2 `snmpd` template

The file `snmp/snmpd.conf.template` is converted to a node-specific configuration by `scripts/generate_snmpd_config.sh`.

The important directives are:

```conf
agentaddress udp:1161

rocommunity hotelMonitor 127.0.0.1 .1.3.6.1.4.1.8072.9999.4
rocommunity hotelMonitor 192.168.122.0/24 .1.3.6.1.4.1.8072.9999.4

sysLocation Hotel_Floor_1_Master
sysContact embedded-lab
sysName master

dontLogTCPWrappersConnects yes

pass .1.3.6.1.4.1.8072.9999.4 \
    /home/amir/04/snmp/sensor_snmp_pass \
    --db /home/amir/master/master.db \
    --node master \
    --base-oid .1.3.6.1.4.1.8072.9999.4
```

The actual generated configuration is written to:

```text
generated/<node-name>/snmpd.conf
```

For example:

```text
generated/master/snmpd.conf
generated/slave1/snmpd.conf
generated/slave2/snmpd.conf
```

## 2.3 Explanation of the important directives

### `agentaddress`

```conf
agentaddress udp:1161
```

This makes the project agent listen on UDP port `1161` on the VM network interfaces.

### `rocommunity`

```conf
rocommunity hotelMonitor 127.0.0.1 .1.3.6.1.4.1.8072.9999.4
rocommunity hotelMonitor 192.168.122.0/24 .1.3.6.1.4.1.8072.9999.4
```

These lines provide read-only access:

- Local requests are allowed from `127.0.0.1`.
- Network requests are allowed from `192.168.122.0/24`.
- Access is restricted to the custom sensor OID subtree.

### `pass`

```conf
pass <base-oid> <handler> --db <database> --node <node> --base-oid <base-oid>
```

The `pass` directive delegates requests under the custom OID subtree to the C++ program. Net-SNMP automatically appends one of the following operations to the command:

- `-g <OID>` for an exact GET request.
- `-n <OID>` for a GETNEXT request.
- `-s <OID> <TYPE> <VALUE>` for a SET request.

This implementation is read-only. SET requests are rejected with `not-writable`.

## 2.4 Runtime files and permissions

The start script uses a writable node-specific persistent directory:

```text
generated/<node-name>/persistent/
```

This avoids permission errors that can occur when an unprivileged user tries to write Net-SNMP cache and persistent files under `/var/lib/snmp`.

The generated configuration is assigned restrictive permissions because it contains the SNMP community string:

```bash
chmod 600 generated/<node-name>/snmpd.conf
```

---

# 3. Structure of the Defined OIDs

## 3.1 Base OID

All project objects are located below:

```text
.1.3.6.1.4.1.8072.9999.4
```

The hierarchy is:

```text
iso(1)
└── org(3)
    └── dod(6)
        └── internet(1)
            └── private(4)
                └── enterprises(1)
                    └── netSnmp(8072)
                        └── netSnmpPlaypen(9999)
                            └── hotelSensorMIB(4)
```

The symbolic MIB description is stored in:

```text
snmp/HOTEL-SENSOR-MIB.txt
```

Numeric OIDs are used in the test commands with the `-On` option, so loading the MIB file is not required for normal operation.

## 3.2 Sensor count scalar

| Object | OID | Type | Description |
|---|---|---|---|
| Sensor count | `.1.3.6.1.4.1.8072.9999.4.1.0` | INTEGER | Number of active sensors available on the local node |

Example:

```text
.1.3.6.1.4.1.8072.9999.4.1.0 = INTEGER: 4
```

## 3.3 Sensor table

The table entry prefix is:

```text
.1.3.6.1.4.1.8072.9999.4.2.1
```

A complete table object has this form:

```text
<base-oid>.2.1.<column>.<index>
```

The local table index starts at `1`. In the tested databases, every node has four sensors, so the indexes are `1` to `4`.

| Column | OID suffix | Type | Description |
|---:|---|---|---|
| 1 | `.2.1.1.<index>` | INTEGER | Local SNMP table index |
| 2 | `.2.1.2.<index>` | STRING | Sensor ID |
| 3 | `.2.1.3.<index>` | STRING | Sensor name |
| 4 | `.2.1.4.<index>` | STRING | Sensor description: type, location, and node |
| 5 | `.2.1.5.<index>` | STRING | Latest stored value |
| 6 | `.2.1.6.<index>` | STRING | Measurement unit |
| 7 | `.2.1.7.<index>` | STRING | Timestamp of the latest reading |
| 8 | `.2.1.8.<index>` | STRING | Sensor type |
| 9 | `.2.1.9.<index>` | STRING | Node name |

The three fields required by the assignment are provided by columns 3, 4, and 5:

```text
Sensor name:        <base-oid>.2.1.3.<index>
Sensor description: <base-oid>.2.1.4.<index>
Latest value:       <base-oid>.2.1.5.<index>
```

## 3.4 Example objects

For the first sensor on the Master:

```text
Sensor ID:
.1.3.6.1.4.1.8072.9999.4.2.1.2.1 = STRING: "101"

Sensor name:
.1.3.6.1.4.1.8072.9999.4.2.1.3.1 = STRING: "Floor1_Room101_Temp"

Sensor description:
.1.3.6.1.4.1.8072.9999.4.2.1.4.1 = STRING: "type=temperature; location=Floor1_Room101; node=master"

Latest value:
.1.3.6.1.4.1.8072.9999.4.2.1.5.1 = STRING: "24.8"

Unit:
.1.3.6.1.4.1.8072.9999.4.2.1.6.1 = STRING: "C"

Recorded time:
.1.3.6.1.4.1.8072.9999.4.2.1.7.1 = STRING: "2026-06-01 10:15:00"
```

The same OID structure is used on every node. The SNMP endpoint identifies which local database is being queried.

---

# 4. Running the SNMP Service

The commands in this section must be run inside the corresponding VM.

## 4.1 Build only

```bash
cd /home/amir/04
make
```

The executable is created at:

```text
snmp/sensor_snmp_pass
```

To remove build and generated files:

```bash
make clean
```

## 4.2 Build and start automatically

The recommended command compiles the handler, generates the node-specific configuration, and starts the custom SNMP agent.

### On Master

```bash
cd /home/amir/04
chmod +x scripts/*.sh
./scripts/build_and_run.sh config/master.env
```

### On Slave1

```bash
cd /home/amir/04
chmod +x scripts/*.sh
./scripts/build_and_run.sh config/slave1.env
```

### On Slave2

```bash
cd /home/amir/04
chmod +x scripts/*.sh
./scripts/build_and_run.sh config/slave2.env
```

A successful start displays the node, database, listening address, base OID, PID, and log file. Example:

```text
Generated SNMP configuration: /home/amir/04/generated/master/snmpd.conf
SNMP agent started.
Node:       master
Database:   /home/amir/master/master.db
Listen:     udp:1161
Base OID:   .1.3.6.1.4.1.8072.9999.4
PID:        <process-id>
Log:        /home/amir/04/generated/master/snmpd.log
```

## 4.3 Start without rebuilding

```bash
./scripts/start_snmpd.sh config/master.env
```

Use the matching configuration file on each Slave.

## 4.4 Stop the agent

### Master

```bash
./scripts/stop_snmpd.sh config/master.env
```

### Slave1

```bash
./scripts/stop_snmpd.sh config/slave1.env
```

### Slave2

```bash
./scripts/stop_snmpd.sh config/slave2.env
```

## 4.5 Verify the process and UDP port

```bash
ss -lunp | grep ':1161'
```

View the node log:

```bash
tail -n 50 generated/master/snmpd.log
```

Replace `master` with `slave1` or `slave2` on the corresponding VM.

## 4.6 Verify the sensor count locally

```bash
snmpget \
  -v2c \
  -c hotelMonitor \
  -On \
  udp:127.0.0.1:1161 \
  .1.3.6.1.4.1.8072.9999.4.1.0
```

Expected result on each tested node:

```text
.1.3.6.1.4.1.8072.9999.4.1.0 = INTEGER: 4
```

---

# 5. Reading Information with `snmpwalk`

## 5.1 Read all local sensor objects

Run this command on any node to read its local SNMP agent:

```bash
snmpwalk \
  -v2c \
  -c hotelMonitor \
  -On \
  -t 2 \
  -r 1 \
  udp:127.0.0.1:1161 \
  .1.3.6.1.4.1.8072.9999.4
```

Options:

- `-v2c`: use SNMP version 2c.
- `-c hotelMonitor`: use the configured community string.
- `-On`: print numeric OIDs.
- `-t 2`: use a two-second timeout.
- `-r 1`: retry once.
- `udp:<host>:1161`: select the target agent.

## 5.2 Read only the sensor table

```bash
snmpwalk \
  -v2c \
  -c hotelMonitor \
  -On \
  udp:127.0.0.1:1161 \
  .1.3.6.1.4.1.8072.9999.4.2.1
```

## 5.3 Read one exact value with `snmpget`

The following command reads the latest value of table index 1:

```bash
snmpget \
  -v2c \
  -c hotelMonitor \
  -On \
  udp:127.0.0.1:1161 \
  .1.3.6.1.4.1.8072.9999.4.2.1.5.1
```

## 5.4 Read the Slave agents from Master

### Slave1

```bash
snmpwalk \
  -v2c \
  -c hotelMonitor \
  -On \
  udp:192.168.122.18:1161 \
  .1.3.6.1.4.1.8072.9999.4
```

### Slave2

```bash
snmpwalk \
  -v2c \
  -c hotelMonitor \
  -On \
  udp:192.168.122.190:1161 \
  .1.3.6.1.4.1.8072.9999.4
```

The final message below is normal and indicates that `snmpwalk` reached the end of the custom subtree:

```text
No more variables left in this MIB View (It is past the end of the MIB tree)
```

---

# 6. Running the Test Scripts

## 6.1 Test the C++ `pass` handler directly

This test does not require a running `snmpd` process. It verifies exact GET, GETNEXT, required sensor fields, and read-only behavior.

### Master

```bash
cd /home/amir/04
./scripts/test_pass_handler.sh config/master.env
```

### Slave1

```bash
./scripts/test_pass_handler.sh config/slave1.env
```

### Slave2

```bash
./scripts/test_pass_handler.sh config/slave2.env
```

Expected result:

```text
PASS: direct Net-SNMP pass protocol test succeeded.
Node: <node-name>
Sensors: 4
Required fields: name, description, latest value
```

The top-level Makefile can also build the code and run the Master direct-handler test:

```bash
make test
```

## 6.2 Configure the distributed node list

The file `config/nodes.env` is used by the distributed test scripts:

```bash
SNMP_VERSION=2c
SNMP_COMMUNITY=hotelMonitor
SNMP_BASE_OID=.1.3.6.1.4.1.8072.9999.4
SNMP_TIMEOUT=2
SNMP_RETRIES=1

NODES=(
    "master|127.0.0.1|1161"
    "slave1|192.168.122.18|1161"
    "slave2|192.168.122.190|1161"
)
```

Run the following distributed tests on the **Master VM** after all three agents have been started.

## 6.3 Validate all nodes automatically

```bash
cd /home/amir/04
./scripts/test_snmp.sh config/nodes.env
```

The script verifies that:

1. The sensor count is greater than zero.
2. Every sensor has a name.
3. Every sensor has a description.
4. Every sensor has a latest value.
5. Master can communicate with both Slave agents over the network.

Expected result from the tested system:

```text
Testing master at 127.0.0.1:1161 ...
  PASS: 4 sensors; name/description/latest-value are available for all.
Testing slave1 at 192.168.122.18:1161 ...
  PASS: 4 sensors; name/description/latest-value are available for all.
Testing slave2 at 192.168.122.190:1161 ...
  PASS: 4 sensors; name/description/latest-value are available for all.
All SNMP node tests passed.
```

## 6.4 Read every sensor from all nodes

Run on the Master VM:

```bash
cd /home/amir/04
./scripts/read_all_sensors.sh config/nodes.env
```

This script performs an `snmpwalk` for Master, Slave1, and Slave2 and saves separate outputs in:

```text
test-output/master_snmpwalk.txt
test-output/slave1_snmpwalk.txt
test-output/slave2_snmpwalk.txt
```

To save the complete terminal output in one additional file:

```bash
mkdir -p test-output
./scripts/read_all_sensors.sh config/nodes.env \
  | tee test-output/final_snmp_test.txt
```

Verify that all twelve tested sensor IDs are present:

```bash
grep -E 'STRING: "(101|102|103|104|201|202|203|204|301|302|303|304)"' \
  test-output/final_snmp_test.txt
```

## 6.5 Final connectivity check from Master

```bash
snmpget -v2c -c hotelMonitor -On \
  udp:127.0.0.1:1161 \
  .1.3.6.1.4.1.8072.9999.4.1.0

snmpget -v2c -c hotelMonitor -On \
  udp:192.168.122.18:1161 \
  .1.3.6.1.4.1.8072.9999.4.1.0

snmpget -v2c -c hotelMonitor -On \
  udp:192.168.122.190:1161 \
  .1.3.6.1.4.1.8072.9999.4.1.0
```

All three commands should return:

```text
INTEGER: 4
```

---

# 7. Data Path from SQLite to the SNMP Output

The complete data path is:

```text
SQLite database
      |
      v
sensor_snmp_pass C++ handler
      |
      v
Net-SNMP pass directive
      |
      v
snmpd agent on UDP port 1161
      |
      v
SNMP GET / GETNEXT response
      |
      v
snmpget / snmpwalk output
```

The detailed sequence is as follows:

1. `snmpget` sends an SNMP GET request, or `snmpwalk` sends repeated GETNEXT requests, to UDP port `1161`.
2. `snmpd` checks the community string, source address, and requested OID subtree.
3. When the requested OID is below `.1.3.6.1.4.1.8072.9999.4`, the `pass` directive executes `snmp/sensor_snmp_pass`.
4. Net-SNMP adds `-g` for GET or `-n` for GETNEXT, together with the requested OID.
5. The handler opens the configured SQLite database in **read-only mode**.
6. The handler reads active sensors from the `sensors` table.
7. For every sensor, it selects the latest row from `sensor_readings` using:

   ```sql
   ORDER BY recorded_at DESC, id DESC
   LIMIT 1
   ```

8. The handler converts the query result into the sensor count scalar and the indexed SNMP table objects.
9. For a GET request, the handler returns the exact requested OID. For a GETNEXT request, it returns the next OID in numeric order.
10. The handler writes the three-line response format required by Net-SNMP `pass`:

    ```text
    <OID>
    <TYPE>
    <VALUE>
    ```

11. `snmpd` converts this result into an SNMP response packet and sends it to the manager.
12. `snmpget` or `snmpwalk` prints the final OID, type, and value.

Because the C++ handler queries SQLite for each request, newly inserted readings become visible through SNMP without restarting the agent. This was verified by inserting a temporary newer reading, reading it through SNMP, deleting it, and observing that SNMP immediately returned the previous latest value again.

The implementation is read-only: it exposes database information but does not modify sensor data through SNMP.

---

# Troubleshooting

## Timeout or no response

Check that the custom agent is running:

```bash
ss -lunp | grep ':1161'
```

Check network connectivity from Master:

```bash
ping -c 3 192.168.122.18
ping -c 3 192.168.122.190
```

If UFW is enabled, allow the project port:

```bash
sudo ufw allow from 192.168.122.0/24 to any port 1161 proto udp
```

## `No Such Instance` or an empty walk

Verify the configured database path:

```bash
grep '^DATABASE_PATH=' config/master.env
ls -lh /home/amir/master/master.db
```

Run the handler test directly:

```bash
./scripts/test_pass_handler.sh config/master.env
```

Inspect the agent log:

```bash
cat generated/master/snmpd.log
```

## Permission errors under `/var/lib/snmp`

The project start script uses a writable private directory under:

```text
generated/<node-name>/persistent/
```

Make sure the latest `scripts/start_snmpd.sh` is installed on all three VMs.

## Port already in use

```bash
ss -lunp | grep ':1161'
```

Stop the previous project agent with the correct node configuration:

```bash
./scripts/stop_snmpd.sh config/master.env
```

## End-of-tree message

This message is not an error:

```text
No more variables left in this MIB View
```

It means the walk completed successfully and reached the end of the permitted custom OID subtree.

---

# Security Notes

SNMPv2c uses a plaintext community string and is suitable for this isolated laboratory network. For a production deployment, SNMPv3 should be used with authentication and encryption. The community string should also be changed from the example value and access should remain restricted to trusted management hosts and the custom OID subtree.
