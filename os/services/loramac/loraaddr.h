#include "contiki.h"

#ifndef LORAADDR_H_
#define LORAADDR_H_
/*---------------------------------------------------------------------------*/
#define LORA_ADDR_PREFIX_SIZE 1
#define LORA_ADDR_ID_SIZE 2
#define LORA_ADDR_SIZE (LORA_ADDR_PREFIX_SIZE + LORA_ADDR_ID_SIZE)
/*---------------------------------------------------------------------------*/
typedef struct lora_addr{
    uint8_t prefix;
    uint16_t id;
}lora_addr_t;
/*---------------------------------------------------------------------------*/
void loraaddr_copy(lora_addr_t *dest, const lora_addr_t *from);

int loraaddr_compare(lora_addr_t *addr1, lora_addr_t *addr2);

void loraaddr_set_node_addr(lora_addr_t *addr);

void loraaddr_print(const lora_addr_t *addr);

bool loraaddr_is_in_dag(lora_addr_t *addr);
/*---------------------------------------------------------------------------*/
extern const lora_addr_t lora_root_addr;
extern lora_addr_t lora_node_addr;
extern const lora_addr_t lora_null_addr;
/*---------------------------------------------------------------------------*/
#endif /* LORAADDR_H_ */
