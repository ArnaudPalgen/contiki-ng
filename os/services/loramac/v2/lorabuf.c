#include "lorabuf.h"
#include "loraaddr.h"
#include "sys/cc.h"


lorabuf_attr_t lorabuf_attrs[LORABUF_NUM_ATTRS];
lora_addr_t lorabuf_addrs[LORABUF_NUM_ADDRS];

static uint8_t lorabuf[LORABUF_SIZE];
static uint16_t datalen;

/*---------------------------------------------------------------------------*/
void
lorabuf_clear(void) //done
{
    datalen = 0;
    memset(lorabuf_attrs, 0, sizeof(lorabuf_attrs));
    memset(lorabuf_addrs, 0, sizeof(lorabuf_addrs));

}
/*---------------------------------------------------------------------------*/
void
lorabuf_set_data_len(uint16_t len) //done
{
    datalen = len;
}
/*---------------------------------------------------------------------------*/
uint16_t
lorabuf_get_data_len(void) //done
{
    return datalen;
}
/*---------------------------------------------------------------------------*/
int
lorabuf_copy_from(const void* from, uint16_t len) //done
{
    uint16_t l;
    lorabuf_clear();
    l = MIN(LORABUF_SIZE, len);
    memcpy(lorabuf, from, l);
    datalen = l;
    return l;

}
/*---------------------------------------------------------------------------*/
void
lorabuf_set_attr(uint8_t type, lorabuf_attr_t val) //done
{
    lorabuf_attrs[type] = val;
}
/*---------------------------------------------------------------------------*/
lorabuf_attr_t
lorabuf_get_attr(uint8_t type) //done
{
    return lorabuf_attrs[type];
}
/*---------------------------------------------------------------------------*/
void
lorabuf_set_addr(uint8_t type, const lora_addr_t *addr) //done
{
    loraaddr_copy(&lorabuf_addrs[type - LORABUF_ADDR_FIRST], addr);
}
/*---------------------------------------------------------------------------*/
const lora_addr_t *
lorabuf_get_addr(uint8_t type) //done
{
    return &lorabuf_addrs[type - LORABUF_ADDR_FIRST];
}
/*---------------------------------------------------------------------------*/
void
print_lorabuf(void)//todo
{
    printf("LoRaBUF print function not implemented\n");
}
