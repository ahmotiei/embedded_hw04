#!/bin/bash


sqlite3 master.db <<EOF


CREATE TABLE IF NOT EXISTS sensors
(
id INTEGER,
type TEXT,
value REAL,
timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
);



INSERT INTO sensors
(id,type,value)
VALUES
(10,'temperature',30),
(11,'humidity',50);



EOF


echo "Master database created"