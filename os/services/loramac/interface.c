#include "net/ipv6/uip.h"
#include "net/netstack.h"
#include "loramac.h"
#include "sys/log.h"
#include "dev/button-hal.h"
#include "net/routing/routing.h"
#include <stdlib.h>

/*---------------------------------------------------------------------------*/
#define LOG_MODULE "Interface"
#define LOG_LEVEL LOG_LEVEL_DBG
#define LOG_CONF_WITH_COLOR 3

PROCESS(loramac_process, "LoRaMAC-interface");
/*---------------------------------------------------------------------------*/
/*
Conversion function between lora_addr_t and uip_ip6addr_t.
An IPv6 address is build as follow:

|<-----1----->|<--6-->|<-----1----->|<----------6---------->|<---2--->|
| IPv6 PREFIX | ZEROS | LORA PREFIX | COMMON_LINK_ADDR_PART | NODE_ID |
 0           0 1     6 7           7 8                    13 14     15

Where:
  - IPv6 PREFIX is 0xFD
  - LORA PREFIX is The prefix of the lora_addr_t
  - COMMON_LINK_ADDR_PART is the 6 first bytes of the link layer address and defined as follow: 0x02, 0x12, 0x4B, 0x00, 0x06, 0x0D
  - NODE_ID is the node-id of the node
*/

void lora2ipv6(lora_addr_t *src_addr, uip_ip6addr_t *dest_addr)//current
{
  uip_ip6addr_u8(dest_addr, 0xFD, 0, 0, 0, 0, 0, 0, src_addr->prefix, 0x02, 0x12, 0x4B, 0x00, 0x06, 0x0D, src_addr->id>>8, src_addr->id);
  
}

void ipv62lora(uip_ip6addr_t *src_addr, lora_addr_t *dest_addr)//done
{
  LOG_INFO("CONVERT SRC ADDR:");
  LOG_INFO_6ADDR(src_addr);
  dest_addr->prefix = src_addr->u8[7];
  dest_addr->id = (src_addr->u8[14] <<8) + src_addr->u8[15];
}

/*---------------------------------------------------------------------------*/
static void
init(void)
{
  LOG_INFO("Welcome to LoRaMAC interface !\n");
  //mac_init();
  #ifdef IS_ROOT
    LOG_INFO("Init LoRaMAC Interface\n");
    
    process_start(&loramac_process, NULL);
  #else
    LOG_INFO("Not the root -> Do nothing for LoRaMAC\n");
  #endif
}

/*---------------------------------------------------------------------------*/
/* 
 * Send fallback IP packets to the LoRa ROOT with the LoRaMAC protocol.
 * UIP_CONF_BUFFER_SIZE must be set to 279. This  is the 247 bytes of the LoRaMAC
 * payload to which we add 32 bytes of the src/dest IP addresses that are not used.
 */
static int
output(void)//done
{ 

  LOG_INFO("Receive data for loramac whouhouuuuuu\n");
  static char data[(UIP_CONF_BUFFER_SIZE-32)*2];
  int uip_index = 0;
  int data_index = 0;
  
  while(uip_index<uip_len){
    if(uip_index==8){
      // skip src and dest ipv6 addr
      uip_index= UIP_IPH_LEN;
    }

    sprintf(data+data_index, "%02X", uip_buf[uip_index]);
    data_index+=2;
    uip_index++;
  }
  lora_addr_t src_addr;
  ipv62lora(&(UIP_IP_BUF->srcipaddr), &src_addr);
  mac_send_packet(src_addr, true, &data);

  
  return 0;
}
/*---------------------------------------------------------------------------*/

static void
loramac_input_callback(lora_addr_t *src, lora_addr_t *dest, char* data)//current
{//data from loramac -> ipv6

  LOG_INFO("Receive data FROM loramac whouhouuuuuu\n");
    uip_ip6addr_t ip_src, ip_dest;
    uint16_t i = 0;
    char current_byte[2];

    lora2ipv6(src, &ip_src);
    lora2ipv6(dest, &ip_dest);

    while(i<UIP_BUFSIZE && *data !=0){
        if(i==8){//we have to write to ipv6 addresses
            memcpy(&(uip_buf[i]), &(ip_src.u8), 16);
            i+=16;
            memcpy(&(uip_buf[i]), &(ip_dest.u8), 16);
            i+=16;
            continue;
        }
        memcpy(current_byte, data, 2);
        data+=2;
        uip_buf[i]= (uint8_t) strtol(current_byte, NULL, 16);
        i+=1;
    }
    if(i < 8){//The IPv6 header is not complete
        LOG_WARN("IPV6 header not complete\n");
        uipbuf_clear();
    }else{//deliver the packet to the tcp/ip stack
        LOG_INFO("Deliver data to the TCP/IP stack\n");
        tcpip_input();
    }
}

/*---------------------------------------------------------------------------*/
const struct uip_fallback_interface loramac_interface = {
    init, output
};
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(loramac_process, ev, data)//current
{
  PROCESS_BEGIN();
  
  LOG_INFO("Welcome !\n");
  NETSTACK_MAC.off();
  
  PROCESS_WAIT_EVENT_UNTIL(ev == button_hal_press_event);
  LOG_INFO("Button pushed\n");
  
  loramac_set_input_callback(loramac_input_callback);
  mac_root_start();
  
  PROCESS_WAIT_EVENT_UNTIL(ev == loramac_network_joined);
  LOG_INFO("RECEIVE LORAMAC JOINED EVENT !\n");

  LOG_INFO("PREFIX: %d\n", loramac_addr.prefix);
  uip_ipaddr_t prefix;
  uip_ip6addr_u8(&prefix, 0xFD, 0, 0, 0, 0, 0, 0, loramac_addr.prefix, 0,0,0,0,0,0,0,0);
  NETSTACK_ROUTING.root_set_prefix(&prefix, NULL);
  NETSTACK_ROUTING.root_start();
  NETSTACK_MAC.on();
  
  PROCESS_END();
}