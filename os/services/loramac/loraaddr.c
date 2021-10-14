#include "loraaddr.h"

lora_addr_t lora_node_addr;

const lora_addr_t lora_root_addr = {1, 0};
const lora_addr_t lora_null_addr = {0,0};
/*---------------------------------------------------------------------------*/
void
loraaddr_copy(lora_addr_t *dest, const lora_addr_t *from)
{
    memcpy(dest, from, LORA_ADDR_SIZE);
}
/*---------------------------------------------------------------------------*/
int
loraaddr_compare(lora_addr_t *addr1, lora_addr_t *addr2)
{
    return (memcmp(addr1, addr2, LORA_ADDR_SIZE) == 0);
}
/*---------------------------------------------------------------------------*/
void
loraaddr_set_node_addr(lora_addr_t *addr)
{
    memcpy(&lora_node_addr, addr, LORA_ADDR_SIZE);
}
/*---------------------------------------------------------------------------*/
void
loraaddr_print(const lora_addr_t *addr)
{
    printf("%d:%d", addr->prefix, addr->id);
}
