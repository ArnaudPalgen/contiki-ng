#ifndef LORAFRAMER_H_
#define LORAFRAMER_H_

#include "contiki.h"

/* Parse the received data to a LoRaMAC frame in the lorabuf */
int parse(char *data, int len, int offset);

/* Create a string of char from the lorabuf */
int create(char* destination);

#endif /* LORAFRAMER_H_ */
