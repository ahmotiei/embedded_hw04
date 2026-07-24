# Embedded Systems Homework 04

## Distributed Sensor Monitoring System

This repository contains the complete implementation of Homework 04 for
the Embedded Systems course.

The project implements a distributed sensor monitoring platform
consisting of multiple independent modules. The system starts from a
basic distributed database architecture and gradually extends its
capabilities with caching, MQTT communication, SNMP monitoring, REST API
services, and an alert daemon.

Repository: https://github.com/ahmotiei/embedded_hw04

------------------------------------------------------------------------

# Project Overview

The main objective of this project is to design and implement a
distributed sensor management system for a hotel monitoring scenario.

The final system contains:

-   A Master node responsible for receiving operator requests and
    coordinating responses.
-   Two Slave nodes responsible for storing and providing local sensor
    data.
-   Local SQLite databases for sensor data storage.
-   Memory caching using Memcached.
-   MQTT communication through Mosquitto broker.
-   SNMP-based sensor monitoring.
-   HTTP REST API services using Mongoose library.
-   A background alert monitoring daemon managed by systemd.

All implementation parts are developed using C/C++ as required by the
project specification.

------------------------------------------------------------------------

# Project Structure

    embedded_hw04/
    │
    ├── 01/                 # Basic distributed database system
    ├── 02/                 # Two-layer database architecture with caching
    ├── 03/                 # MQTT integration
    ├── 04/                 # SNMP sensor monitoring
    ├── 05/                 # Sensor history REST API
    ├── 06/                 # Alert monitoring daemon
    │
    ├── sqlite_init_files_master_slaves/
    │                       # Initial SQLite database files and helper scripts
    │
    ├── research.pdf        # Preliminary research document
    └── تمرین چهارم.pdf     # Homework specification

Each section is independent and contains its own:

-   Source code
-   Makefile
-   Configuration files
-   Bash scripts
-   README documentation
-   Test scripts
-   Report files

------------------------------------------------------------------------

# Section 01 - Distributed Database System

Directory:

    01/

This section implements the basic distributed sensor database
architecture.

Architecture:

    Operator
        |
        |
     Master Node
        |
        +---------+
                  |
              Slave Nodes
            Slave1 Slave2

Features:

-   One Master node and two Slave nodes
-   SQLite database on each node
-   HTTP communication between nodes
-   Sensor query forwarding mechanism
-   Dynamic configuration of IP addresses and ports
-   Database initialization scripts
-   Automated build and test scripts

Request flow:

1.  Operator sends a sensor request to Master.
2.  Master checks its local database.
3.  If data exists, Master returns the response.
4.  Otherwise, Master forwards the request to Slave nodes.
5.  The final response is returned to the operator through Master.

------------------------------------------------------------------------

# Section 02 - Two-Layer Database and Caching

Directory:

    02/

This section improves system performance by adding a caching layer.

Architecture:

    Application
         |
     Memcached Cache
         |
     SQLite Database

Features:

-   SQLite as persistent storage
-   Memcached as RAM cache
-   Cache-first reading strategy
-   Cache miss handling
-   Performance benchmark scripts
-   Comparison between cached and non-cached requests

The benchmark evaluates response time improvement between:

-   First access (SQLite read)
-   Second access (Cache read)

------------------------------------------------------------------------

# Section 03 - MQTT Integration

Directory:

    03/

This section connects the sensor system to MQTT communication.

Technologies:

-   MQTT 3.1.1
-   Mosquitto Broker

Features:

-   MQTT request/response communication
-   Sensor query topics
-   Broker configuration
-   MQTT testing scripts
-   Response time measurement

Communication flow:

    Client
     |
    MQTT Broker
     |
    Master
     |
    Slave Nodes

------------------------------------------------------------------------

# Section 04 - SNMP Sensor Monitoring

Directory:

    04/

This section implements sensor monitoring using SNMP.

Features:

-   Custom SNMP MIB definitions
-   Custom OID implementation
-   snmpd configuration
-   Pass-based SNMP handler
-   Sensor reading using snmpwalk
-   Automated SNMP testing

The sensor information exposed through SNMP includes:

-   Sensor name
-   Sensor description
-   Latest sensor value

------------------------------------------------------------------------

# Section 05 - Sensor History REST API

Directory:

    05/

This section provides an HTTP API for retrieving historical sensor data.

Implementation:

-   C++ application
-   Mongoose HTTP library
-   SQLite database access

API capabilities:

-   Receive sensor name
-   Receive sensor ID
-   Receive requested date
-   Return stored sensor values in JSON format

Example response:

``` json
{
  "sensor_name": "temperature",
  "sensor_id": "101",
  "date": "2026-06-01",
  "values": [
    {
      "time": "10:15:00",
      "value": "24.8"
    }
  ]
}
```

------------------------------------------------------------------------

# Section 06 - Alert Monitoring Daemon

Directory:

    06/

This section implements an automatic sensor monitoring daemon.

Features:

-   Periodic sensor checking
-   Configurable alert rules
-   Alert database storage
-   Duplicate alert prevention
-   Alert resolution handling
-   systemd service integration

Stored alert information:

-   Sensor ID
-   Sensor name
-   Alert type
-   Sensor value
-   Alert creation time
-   Alert status

Service management:

-   Installation
-   Start
-   Stop
-   Status monitoring
-   Journal log inspection

------------------------------------------------------------------------

# Build and Execution

Each section provides its own scripts for building and testing.

Typical workflow:

``` bash
cd <section_directory>

make

./scripts/build_and_run.sh
```

Testing scripts are provided inside each section:

    scripts/

Detailed execution instructions are available in the README file of each
section.

------------------------------------------------------------------------

# Configuration Management

To avoid hard-coded runtime parameters, the project uses configuration
files.

Examples:

-   IP addresses
-   Ports
-   Database paths
-   Service parameters
-   MQTT settings
-   Daemon configuration

Configuration templates are provided as:

    config.example

Users can create their own configuration files based on these templates.

------------------------------------------------------------------------

# Dependencies

Main dependencies include:

-   GCC / G++
-   Make
-   SQLite3
-   Mongoose Library
-   Memcached
-   Mosquitto MQTT Broker
-   Net-SNMP
-   systemd

Installation requirements are explained inside each section README file.

------------------------------------------------------------------------

# Testing

The project includes automated tests for:

-   Database initialization
-   Master/Slave communication
-   Cache performance
-   MQTT communication
-   SNMP sensor reading
-   API responses
-   Alert generation
-   systemd daemon lifecycle

Test outputs are stored inside:

    test-output/

directories of related sections.

------------------------------------------------------------------------

# Reports

Each section contains a report describing:

-   System architecture
-   Component interaction
-   Data flow
-   Request/response path
-   Database design
-   Configuration management
-   Performance analysis
-   Security considerations

------------------------------------------------------------------------

# Compliance with Homework Requirements

This project follows the required structure and implementation rules:

-   Six independent sections are provided.
-   Each section contains its own README documentation.
-   Each section contains Makefile based compilation.
-   Bash scripts are provided for build and execution.
-   C/C++ is used for implementation.
-   Configuration values are separated from source code.
-   Database initialization is performed using provided scripts.
-   Test procedures and outputs are included.

------------------------------------------------------------------------

# Author

Amir-Hossein Motiei

Embedded Systems Course\
Sharif University of Technology
