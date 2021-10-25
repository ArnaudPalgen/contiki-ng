#include <stdlib.h>
#include "lorabuf.h"
#include "framer.h"
#include "loraaddr.h"
#include "loramac.h"
#include "sys/log.h"
/*---------------------------------------------------------------------------*/
/*logging configuration*/
#define LOG_MODULE "Framer"
#define LOG_LEVEL LOG_LEVEL_DBG
#define LOG_CONF_WITH_COLOR 3
/*---------------------------------------------------------------------------*/
int
parse(char *data, int payload_len)
{
    LOG_DBG("enter parse\n");
    LOG_DBG("   > data: %s\n", data);
    LOG_DBG("   > payload_len: %d\n", payload_len);

    char prefix_c[2];
    uint8_t prefix;
    
    char id_c[4];
    uint16_t id;

    /*extract src addr*/
    memcpy(prefix_c, data, 2);
    data = data+2;
    memcpy(id_c, data, 4);
    data = data+4;

    prefix = (uint8_t) strtol(prefix_c, NULL, 16);
    id = (uint16_t) strtol(id_c, NULL, 16);

    lora_addr_t addr = {prefix, id};
    lorabuf_set_addr(LORABUF_ADDR_SENDER, &addr);

    /*extract dest addr*/
    memcpy(prefix_c, data, 2);
    data = data+2;
    memcpy(id_c, data, 4);
    data = data+4;

    prefix = (uint8_t) strtol(prefix_c, NULL, 16);
    id = (uint16_t) strtol(id_c, NULL, 16);

    lora_addr_t addr2 = {prefix, id};
    lorabuf_set_addr(LORABUF_ADDR_RECEIVER, &addr2);

    /*extact flags and command*/
    char cmd[2];
    memcpy(cmd, data, 2);
    data = data+2;
    uint8_t i_cmd = (uint8_t)strtol(cmd, NULL, 16);

    uint8_t flag_filter = 0x01;
    uint8_t command_filter = 0x0F;

    bool k    = (bool)((i_cmd >> 7) & flag_filter);
    bool next = (bool)((i_cmd >> 6) & flag_filter);
    
    loramac_command_t command = (uint8_t)( i_cmd & command_filter );
    lorabuf_set_attr(LORABUF_ATTR_MAC_CMD, command);
    lorabuf_set_attr(LORABUF_ATTR_MAC_NEXT, next);
    lorabuf_set_attr(LORABUF_ATTR_MAC_CONFIRMED, k);

    /* extract SN */
    char sn_c[2];
    memcpy(sn_c, data, 2);
    data = data+2;
    uint8_t sn = (uint8_t)strtol(sn_c, NULL, 16);
    lorabuf_set_attr(LORABUF_ATTR_MAC_SEQNO, sn);

    /*extract payload*/
    // todo review
    lorabuf_set_data_len(payload_len);
    lorabuf_copy_from(data, payload_len);
    LOG_DBG("parse finished\n");
    return 0;
}
/*---------------------------------------------------------------------------*/
int
create(char* destination)
{
    char* dest = destination;
    LOG_DBG("enter create\n");
    LOG_DBG("   > dest: %p\n", dest);

    char addr_c[6];
    lora_addr_t *addr_p;
    
    /* create src addr*/
    addr_p = lorabuf_get_addr(LORABUF_ADDR_SENDER);
    sprintf(addr_c, "%02X%04X", addr_p->prefix, addr_p->id);
    memcpy(dest,addr_c,6);
    dest=dest+6;

    /* create dest addr*/
    addr_p = lorabuf_get_addr(LORABUF_ADDR_RECEIVER);
    sprintf(addr_c, "%02X%04X", addr_p->prefix, addr_p->id);
    memcpy(dest,addr_c,6);
    dest=dest+6;

    /*create flags and MAC command*/
    char flags_command[2];
    uint16_t k_flag =  0x80;
    uint16_t next_flag =  0x40;
    
    uint8_t f_c = 0;
    lorabuf_attr_t k = lorabuf_get_attr(LORABUF_ATTR_MAC_CONFIRMED);
    lorabuf_attr_t next = lorabuf_get_attr(LORABUF_ATTR_MAC_NEXT);
    lorabuf_attr_t command = lorabuf_get_attr(LORABUF_ATTR_MAC_CMD);

    if(k){
        f_c = f_c | k_flag;
    }
    if(next){
        f_c = f_c | next_flag;
    }
    f_c = f_c | ((uint8_t) command);

    sprintf(flags_command, "%02X", f_c);
    memcpy(dest, flags_command, 2);
    dest = dest+2;

    /* create SN */
    char sn[2];
    lorabuf_attr_t seq = lorabuf_get_attr(LORABUF_ATTR_MAC_SEQNO);
    sprintf(sn, "%02X", seq);
    memcpy(dest, sn, 2);
    dest = dest+2;

    /* create payload */
    uint16_t datalen = lorabuf_get_data_len();
    if (datalen> 0){
        if (datalen%2 !=0){
            memcpy(dest, "0", 1);
            dest = dest+1;
        }
        
        char char_byte[2];
        uint8_t* lorabuf = lorabuf_get_buf();
        for(int i=0;i<datalen;i++){
            sprintf(char_byte,"%02X", lorabuf[i]);
            memcpy(dest, char_byte,2);
            dest = dest+2;
        }
    }
    LOG_DBG("create finished{%s}\n", destination);
    return 0;
}
