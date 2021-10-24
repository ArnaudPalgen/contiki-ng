#include "lorabuf.h"
#include "loraaddr.h"
#include "sys/cc.h"
#include "sys/log.h"
/*---------------------------------------------------------------------------*/
/*logging configuration*/
#define LOG_MODULE "LoRa BUF"
#define LOG_LEVEL LOG_LEVEL_DBG
#define LOG_CONF_WITH_COLOR 3
/*---------------------------------------------------------------------------*/
lorabuf_attr_t lorabuf_attrs[LORABUF_NUM_ATTRS];
lora_addr_t lorabuf_addrs[LORABUF_NUM_ADDRS];

static uint8_t lorabuf[LORA_PAYLOAD_BYTE_MAX_SIZE];
static char lorabuf_c[LORA_FRAME_CHAR_MAX_SIZE+LORA_UART_CHAR_SIZE];

static uint16_t datalen;
static uint16_t datalen_c;
/*---------------------------------------------------------------------------*/
void
lorabuf_c_write_char(char c, int pos)
{
    lorabuf_c[pos] = c;
}
/*---------------------------------------------------------------------------*/
void
lorabuf_c_copy_from(const char* data, unsigned int size)
{
    //lorabuf_clear();
    memcpy(lorabuf_c, data, size);
    datalen_c = size;
}
/*---------------------------------------------------------------------------*/
char*
lorabuf_c_get_buf(void)
{
    return (char *) &lorabuf_c;
}
/*---------------------------------------------------------------------------*/
uint16_t
lorabuf_get_data_c_len(void)
{
    return datalen_c;
}
/*---------------------------------------------------------------------------*/
uint16_t
lorabuf_set_data_c_len(uint16_t len)
{
    datalen_c = len;
    return len;
}
/*---------------------------------------------------------------------------*/
void
lorabuf_clear(void)
{
    LOG_DBG("CLEAR BUFFER\n");
    datalen = 0;
    memset(lorabuf_attrs, 0, sizeof(lorabuf_attrs));
    memset(lorabuf_addrs, 0, sizeof(lorabuf_addrs));
}

void
lorabuf_c_clear(void)
{
    datalen_c = 0;
    memset(lorabuf_c, 0, sizeof(lorabuf_addrs));
}
/*---------------------------------------------------------------------------*/
void
lorabuf_set_data_len(uint16_t len)
{
    datalen = len;
}
/*---------------------------------------------------------------------------*/
uint16_t
lorabuf_get_data_len(void)
{
    return datalen;
}
/*---------------------------------------------------------------------------*/
int
lorabuf_copy_from(const void* from, uint16_t len)
{
    uint16_t l;
    //lorabuf_clear();
    l = MIN(LORA_PAYLOAD_BYTE_MAX_SIZE, len);
    memcpy(lorabuf, from, l);
    datalen = l;
    return l;
}
/*---------------------------------------------------------------------------*/
void
lorabuf_set_attr(uint8_t type, lorabuf_attr_t val)
{
    LOG_DBG("SET ATTR %d to val %d\n", type, val);
    //lorabuf_attrs[type] = val;
    memcpy(&(lorabuf_attrs[type]), &val, sizeof(lorabuf_attr_t));
    LOG_DBG("NEW ATTR value: %d\n", lorabuf_attrs[type]);
}
/*---------------------------------------------------------------------------*/
lorabuf_attr_t
lorabuf_get_attr(uint8_t type)
{
    lorabuf_attr_t val = lorabuf_attrs[type];
    LOG_DBG("RETURN ATTR VALUE FOR TYPE %d: %d\n", type, val);
    return val;
}
/*---------------------------------------------------------------------------*/
void
lorabuf_set_addr(uint8_t type, const lora_addr_t *addr)
{
    loraaddr_copy(&lorabuf_addrs[type - LORABUF_ADDR_FIRST], addr);
}
/*---------------------------------------------------------------------------*/
lora_addr_t *
lorabuf_get_addr(uint8_t type)
{
    return &lorabuf_addrs[type - LORABUF_ADDR_FIRST];
}
/*---------------------------------------------------------------------------*/
void
print_lorabuf(void)
{
    printf("LoRaBUF print function not implemented\n");//todo
}
/*---------------------------------------------------------------------------*/
uint8_t*
lorabuf_get_buf(void)
{
    return (uint8_t *) &lorabuf;
}
/*---------------------------------------------------------------------------*/
//int
//lorabuf_copy_to(const void* to)
//{
//    memcpy(to, lorabuf, datalen);
//    return datalen;
//}
/*---------------------------------------------------------------------------*/
uint8_t*
lorabuf_mac_param_ptr(void)
{
    return &lorabuf_attrs[LORABUF_MAC_PARAMS_FIRST];
}
/*---------------------------------------------------------------------------*/
