#ifndef MQTT_BRIDGE_H
#define MQTT_BRIDGE_H

#include <string>


bool mqtt_init();

void mqtt_loop();

void mqtt_shutdown();


#endif