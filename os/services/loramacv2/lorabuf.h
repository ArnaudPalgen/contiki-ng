#ifndef LORABUF_H_
#define LORABUF_H_

#include "contiki.h"
#include "loraaddr.h"

/*---------------------------------------------------------------------------*/
#define LORA_HDR_BYTE_SIZE 8
#define LORA_PAYLOAD_BYTE_MAX_SIZE 247
#define LORA_FRAME_BYTE_MAX_SIZE (LORA_HDR_BYTE_SIZE + LORA_PAYLOAD_BYTE_MAX_SIZE)

#define LORA_HDR_CHAR_SIZE (2*LORA_HDR_BYTE_SIZE)
#define LORA_PAYLOAD_CHAR_MAX_SIZE (2*LORA_PAYLOAD_BYTE_MAX_SIZE)
#define LORA_FRAME_CHAR_MAX_SIZE (LORA_HDR_CHAR_SIZE + LORA_PAYLOAD_CHAR_MAX_SIZE)

#define LORA_UART_CHAR_SIZE 15

#define LORABUF_NUM_ATTRS 7
#define LORABUF_NUM_ADDRS 2
#define LORABUF_NUM_EXP_UART_RESP 2
/*---------------------------------------------------------------------------*/
typedef uint8_t lorabuf_attr_t;
/*---------------------------------------------------------------------------*/
void lorabuf_c_write_char(char c, unsigned int pos);

void lorabuf_c_copy_from(const char* data, unsigned int size);

char* lorabuf_c_get_buf(void);

uint16_t lorabuf_get_data_c_len(void);
uint16_t lorabuf_set_data_c_len(uint16_t len);
/*---------------------------------------------------------------------------*/
void lorabuf_clear(void);

void lorabuf_set_data_len(uint16_t len);

uint16_t lorabuf_get_data_len(void);

int lorabuf_copy_from(const void* from, uint16_t len);

void lorabuf_set_attr(uint8_t type, lorabuf_attr_t val);

lorabuf_attr_t lorabuf_get_attr(uint8_t type);

void lorabuf_set_addr(uint8_t type, const lora_addr_t *addr);

lora_addr_t * lorabuf_get_addr(uint8_t type);

void print_lorabuf(void);

uint8_t* lorabuf_get_buf(void);

//int lorabuf_copy_to(const void* to);

uint8_t* lorabuf_mac_param_ptr(void);

void lorabuf_c_clear(void);
/*---------------------------------------------------------------------------*/
enum {
    /*UART attributes*/
    LORABUF_ATTR_UART_CMD,
    LORABUF_ATTR_UART_EXP_RESP1,
    LORABUF_ATTR_UART_EXP_RESP2,

    /*frame parameters*/
    LORABUF_ATTR_MAC_CONFIRMED,
    LORABUF_ATTR_MAC_SEQNO,
    LORABUF_ATTR_MAC_NEXT,
    LORABUF_ATTR_MAC_CMD,

    /*frame addresses*/
    LORABUF_ADDR_SENDER,
    LORABUF_ADDR_RECEIVER
};
/*---------------------------------------------------------------------------*/
#define LORABUF_ADDR_FIRST LORABUF_ADDR_SENDER
#define LORABUF_UART_RESP_FIRST LORABUF_ATTR_UART_EXP_RESP1
#define LORABUF_MAC_PARAMS_FIRST LORABUF_ATTR_MAC_CONFIRMED
/*---------------------------------------------------------------------------*/
#endif /* LORABUF_H_ */
