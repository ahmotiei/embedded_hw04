#!/bin/bash


DB=slave.db


sqlite3 $DB <<EOF

CREATE TABLE IF NOT EXISTS sensors
(
id INTEGER,
type TEXT,
value REAL,
timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
);


INSERT INTO sensors VALUES
(1,'temperature',25,NULL),
(2,'humidity',60,NULL),
(3,'pressure',1015,NULL);


EOF


echo "Slave database created"
