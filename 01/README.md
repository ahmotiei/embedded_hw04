# Distributed Database System - Master/Slave Architecture

## 1. Project Overview

This project implements a basic distributed database system consisting of one Master node and two Slave nodes.

The system is designed for sensor data management. Each node contains a local SQLite database, and communication between nodes is performed using HTTP requests through the Mongoose networking library.

The main responsibility of the Master node is to receive requests from the operator, search its local database first, and if the requested sensor data is not available, forward the request to Slave nodes.

The request flow is:

```
Operator
    |
    v
 Master Node
    |
    +----> Local SQLite Database
    |
    +----> Slave 1
    |
    +----> Slave 2
```

The system supports:
- Local database lookup on the Master node
- Communication between Master and Slave nodes
- Sensor data retrieval by sensor ID and sensor type
- Dynamic configuration of IP addresses and ports
- SQLite-based local storage on every node


---

# 2. Project Structure

```
01/
|
├── master/
│   ├── main.cpp
│   ├── http_client.cpp
│   ├── http_client.h
│   ├── Makefile
│   ├── config
│   ├── config.example
│   └── master_init_db.sh
|
├── slave1/
│   ├── main.cpp
│   ├── Makefile
│   ├── config
│   ├── config.example
│   └── slave_init_db.sh
|
├── slave2/
│   ├── main.cpp
│   ├── Makefile
│   ├── config
│   ├── config.example
│   └── slave_init_db.sh
|
├── mongoose/
│   ├── mongoose.c
│   └── mongoose.h
|
├── scripts/
│   ├── build_and_run.sh
│   └── test_requests.sh
|
├── README.md
└── report.md
```

---

# 3. Requirements and Dependencies

The project is developed and tested on Ubuntu 22.04.

Required packages:

- GNU C++ Compiler
- SQLite3
- SQLite development library
- Mongoose HTTP library


Install dependencies:

```bash
sudo apt update

sudo apt install g++ sqlite3 libsqlite3-dev
```

Check SQLite installation:

```bash
sqlite3 --version
```

---

# 4. Configuration

The IP addresses and ports are not hard-coded inside the source code.

All communication parameters are loaded from configuration files.

Example Master configuration:

```ini
PORT=8000

DATABASE=master.db

SLAVE_IP=127.0.0.1

SLAVE1_PORT=9001

SLAVE2_PORT=9002
```

Example Slave configuration:

```ini
PORT=9001

DATABASE=slave.db
```

The configuration files can be modified without recompiling the source code.

---

# 5. Database Initialization

Each node has its own local SQLite database.

Databases:

```
Master:
master.db

Slave1:
slave.db

Slave2:
slave.db
```

The database initialization scripts create the required SQLite database and insert initial sensor data.

Initialize Master database:

```bash
cd master

chmod +x master_init_db.sh

./master_init_db.sh
```

Initialize Slave database:

```bash
cd slave1

chmod +x slave_init_db.sh

./slave_init_db.sh
```

and:

```bash
cd slave2

chmod +x slave_init_db.sh

./slave_init_db.sh
```

---

# 6. Compilation

## Build Master

```bash
cd master

make
```

## Build Slave 1

```bash
cd slave1

make
```

## Build Slave 2

```bash
cd slave2

make
```

The generated executables are:

```
master/master

slave1/slave1

slave2/slave2
```

A build script is also provided:

```bash
cd scripts

chmod +x build_and_run.sh

./build_and_run.sh
```

---

# 7. Running the System

The nodes should be started separately.

## Start Slave 1

Terminal 1:

```bash
cd slave1

./slave1
```

Expected output:

```
Slave running port 9001
```


## Start Slave 2

Terminal 2:

```bash
cd slave2

./slave2
```

Expected output:

```
Slave running port 9002
```


## Start Master

Terminal 3:

```bash
cd master

./master
```

Expected output:

```
Master running on 8000
```

---

# 8. Sending Requests

The operator communicates only with the Master node.

The request format is:

```
GET /api/sensor?id=<sensor_id>&type=<sensor_type>
```

Example:

```bash
curl "http://localhost:8000/api/sensor?id=10&type=temperature"
```

---

# 9. Example Responses

## Data available in Master database

Request:

```bash
curl "http://localhost:8000/api/sensor?id=10&type=temperature"
```

Response:

```json
{
    "source":"master",
    "value":30.00
}
```


---

## Data available in Slave 1

Request:

```bash
curl "http://localhost:8000/api/sensor?id=1&type=temperature"
```

Response:

```json
{
    "source":"slave1",
    "data":{
        "value":25.00
    }
}
```


---

## Data available in Slave 2

Request:

```bash
curl "http://localhost:8000/api/sensor?id=5&type=temperature"
```

Response:

```json
{
    "source":"slave2",
    "data":{
        "value":99.00
    }
}
```


---

## Sensor not found

Request:

```bash
curl "http://localhost:8000/api/sensor?id=999&type=temperature"
```

Response:

```json
{
    "error":"sensor not found"
}
```

---

# 10. Advanced Configuration

The advanced version uses a shared IP address for Slave nodes and different ports for each Slave.

Example:

```
Shared IP:
127.0.0.1

Slave 1:
Port 9001

Slave 2:
Port 9002
```

This allows multiple Slave nodes to be managed using the same network address while distinguishing nodes using different ports.

---

# 11. Testing

A test script is provided:

```bash
cd scripts

chmod +x test_requests.sh

./test_requests.sh
```

The test verifies communication with the Master node and checks sensor data retrieval.

---

# 12. Notes

- The Master node is the only entry point for external requests.
- Each node maintains an independent SQLite database.
- Communication between nodes is performed using HTTP.
- IP addresses and ports are configured externally.
- The system can be deployed on separate Ubuntu 22.04 virtual machines by changing configuration files.