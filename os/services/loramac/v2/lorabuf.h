#ifndef LORABUF_H_
#define LORABUF_H_

#include "contiki.h"

//size for data
#ifdef LORABUF_CONF_SIZE
#define LORABUF_SIZE LORABUF_CONF_SIZE
#else
#define LORABUF_SIZE 1 //TODO
#endif

#define LORABUF_UART_HDR_LEN 1 //TODO
#define LORABUF_MAC_HDR_LEN 1 //TODO

#define LORABUF_NUM_ATTRS 7
#define LORABUF_NUM_ADDRS 2

typedef uint16_t lorabuf_attr_t;

void lorabuf_clear(void);

void lorabuf_set_data_len(uint16_t len);
uint16_t lorabuf_get_data_len(void);

int lorabuf_copy_from(const void* from, uint16_t len);

void lorabuf_set_attr(uint8_t type, lorabuf_attr_t val);
lorabuf_attr_t lorabuf_get_attr(uint8_t type);

void lorabuf_set_addr(uint8_t type, const lora_addr_t *addr);
const lora_addr_t * lorabuf_get_addr(uint8_t type);

void print_lorabuf(void);


enum {
    LORABUF_ATTR_UART_CMD,
    LORABUF_ATTR_UART_EXP_RESP1,
    LORABUF_ATTR_UART_EXP_RESP2,
    LORABUF_ATTR_MAC_CONFIRMED,
    LORABUF_ATTR_MAC_SEQNO,
    LORABUF_ATTR_MAC_NEXT,
    LORABUF_ATTR_MAC_CMD,
    LORABUF_ADDR_SENDER,
    LORABUF_ADDR_RECEIVER
}

#define LORABUF_ADDR_FIRST LORABUF_ADDR_SENDER


#endif
