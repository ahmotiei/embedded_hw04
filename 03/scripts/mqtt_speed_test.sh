#!/bin/bash

BROKER="127.0.0.1"

REQUEST_TOPIC="hotel/sensors/request"
RESPONSE_TOPIC="hotel/sensors/response/#"


SENSORS=(
"101:temperature"
"102:humidity"
"103:motion"
"104:temperature"
)


echo "======================================"
echo " MQTT Multi Sensor Speed Test"
echo "======================================"


test_sensor()
{
    SENSOR_ID=$1
    SENSOR_TYPE=$2
    ROUND=$3


    echo
    echo "Sensor ID: $SENSOR_ID"
    echo "Sensor Type: $SENSOR_TYPE"
    echo "Round: $ROUND"


    mosquitto_sub \
        -h $BROKER \
        -t "$RESPONSE_TOPIC" \
        -C 1 > response.txt &


    SUB_PID=$!


    sleep 0.2


    mosquitto_pub \
        -h $BROKER \
        -t "$REQUEST_TOPIC" \
        -m "{\"sensor_type\":\"$SENSOR_TYPE\",\"sensor_id\":\"$SENSOR_ID\"}"


    wait $SUB_PID


    RESPONSE=$(cat response.txt)


    SOURCE=$(echo "$RESPONSE" \
        | grep -o '"source":"[^"]*"' \
        | cut -d'"' -f4)


    TIME=$(echo "$RESPONSE" \
        | grep -o '"response_time_us":[0-9]*' \
        | cut -d':' -f2)


    SUCCESS=$(echo "$RESPONSE" \
        | grep -o '"success":[^,}]*' \
        | cut -d':' -f2)


    if [ -z "$SOURCE" ]; then
        SOURCE="NOT_FOUND"
    fi


    if [ -z "$TIME" ]; then
        TIME="N/A"
    fi


    echo "Success: $SUCCESS"
    echo "Source: $SOURCE"
    echo "Response time: ${TIME} us"

}



echo
echo "========== First Round (Cache Miss) =========="


for SENSOR in "${SENSORS[@]}"
do
    IFS=":" read SENSOR_ID SENSOR_TYPE <<< "$SENSOR"

    test_sensor \
        $SENSOR_ID \
        $SENSOR_TYPE \
        1

done


sleep 2


echo
echo "========== Second Round (Cache Hit) =========="


for SENSOR in "${SENSORS[@]}"
do
    IFS=":" read SENSOR_ID SENSOR_TYPE <<< "$SENSOR"

    test_sensor \
        $SENSOR_ID \
        $SENSOR_TYPE \
        2

done



echo
echo "======================================"
echo " Test Finished"
echo "======================================"