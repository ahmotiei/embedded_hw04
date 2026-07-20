#!/bin/bash

MASTER_URL="http://192.168.122.22:8000/api/sensor"

OUTPUT="cache_speed_result.csv"


SENSORS=(
"temperature:101"
"humidity:102"
"motion:103"
"temperature:104"

"temperature:201"
"humidity:202"
"motion:203"
"co2:204"

"temperature:301"
"humidity:302"
"motion:303"
"smoke:304"
)


echo "round,sensor_id,sensor_type,source,response_time_us" > $OUTPUT


echo "=============================="
echo "Round 1: Cache Miss Test"
echo "=============================="



for sensor in "${SENSORS[@]}"
do

    TYPE=${sensor%%:*}
    ID=${sensor##*:}


    RESPONSE=$(curl -s \
    "$MASTER_URL?type=$TYPE&id=$ID")


    SOURCE=$(echo $RESPONSE | \
    python3 -c "import sys,json; print(json.load(sys.stdin)['source'])")


    TIME=$(echo $RESPONSE | \
    python3 -c "import sys,json; print(json.load(sys.stdin)['response_time_us'])")


    echo "1,$ID,$TYPE,$SOURCE,$TIME" >> $OUTPUT


    echo "Sensor $ID -> $SOURCE (${TIME} us)"

done



echo ""
echo "=============================="
echo "Round 2: Cache Hit Test"
echo "=============================="


for sensor in "${SENSORS[@]}"
do

    TYPE=${sensor%%:*}
    ID=${sensor##*:}


    RESPONSE=$(curl -s \
    "$MASTER_URL?type=$TYPE&id=$ID")


    SOURCE=$(echo $RESPONSE | \
    python3 -c "import sys,json; print(json.load(sys.stdin)['source'])")


    TIME=$(echo $RESPONSE | \
    python3 -c "import sys,json; print(json.load(sys.stdin)['response_time_us'])")


    echo "2,$ID,$TYPE,$SOURCE,$TIME" >> $OUTPUT


    echo "Sensor $ID -> $SOURCE (${TIME} us)"

done



echo ""
echo "Test completed."
echo "Result saved in $OUTPUT"