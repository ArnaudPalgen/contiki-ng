#include "net/ipv6/uip.h"
#include "net/netstack.h"
#include "loramac.h"
#include "sys/log.h"
#include "dev/button-hal.h"
/*---------------------------------------------------------------------------*/
#define LOG_MODULE "Interface"
#define LOG_LEVEL LOG_LEVEL_DBG
#define LOG_CONF_WITH_COLOR 3

PROCESS(loramac_process, "LoRaMAC-interface");
/*---------------------------------------------------------------------------*/
/*
Conversion function between lora_addr_t and uip_ip6addr_t.
An IPv6 address is build as follow:

|<-----1----->|<--5-->|<-----1----->|<--1-->|<----------6---------->|<---2--->|
| IPv6 PREFIX | ZEROS | LORA PREFIX | ZEROS | COMMON_LINK_ADDR_PART | NODE_ID |
 0           0 1     5 6           6 7     7 8                    13 14     15

Where:
  - IPv6 PREFIX is 0xFD
  - LORA PREFIX is The prefix of the lora_addr_t
  - COMMON_LINK_ADDR_PART is the 6 first bytes of the link layer address and defined as follow: '_','u','m','o','n','s'
  - NODE_ID is the node-id of the node

*/

void lora2ipv6(lora_addr_t *src_addr, uip_ip6addr_t *dest_addr)
{
  uip_ip6addr_u8(dest_addr, 0xFD, 0, 0, 0, 0, 0, src_addr->prefix, 0, '_','u','m','o','n','s', src_addr->id>>8, src_addr->id);

}

void ipv62lora(uip_ip6addr_t *src_addr, lora_addr_t *dest_addr)
{
  dest_addr->prefix = src_addr->u8[6];
  dest_addr->id = src_addr->u8[14] + ((src_addr->u8[15]) << 8);
}

/*---------------------------------------------------------------------------*/
static void
init(void)
{
  LOG_INFO("Init LoRaMAC Interface\n");
  //loramac_set_input_callback(loramac_input_callback)
  //mac_init();
  process_start(&loramac_process, NULL);
}

/*---------------------------------------------------------------------------*/
/* 
 * Send fallback IP packets to the LoRa ROOT with the LoRaMAC protocol.
 * UIP_CONF_BUFFER_SIZE must be set to 279. This  is the 247 bytes of the LoRaMAC
 * payload to which we add 32 bytes of the src/dest IP addresses that are not used.
 */
static int
output(void)
{ 

  LOG_INFO("Receive data for loramac whouhouuuuuu\n");
  char data[(UIP_CONF_BUFFER_SIZE-32)*2];
  int uip_index = 0;
  int data_index = 0;
  
  while(uip_index<UIP_CONF_BUFFER_SIZE){
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
/*
static void
loramac_input_callback(lora_addr_t src, lora_addr_t dest, void* data)
{//data from loramac -> ipv6

}
*/
/*---------------------------------------------------------------------------*/
const struct uip_fallback_interface loramac_interface = {
    init, output
};
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(loramac_process, ev, data)
{
  PROCESS_BEGIN();
  LOG_INFO("Welcome !\n");
  //NETSTACK_MAC.off();
  //LOG_INFO("NetStack OFF\n");
  PROCESS_WAIT_EVENT_UNTIL(ev == button_hal_press_event);
  LOG_INFO("Button pushed\n");
  mac_init();
  
  #ifdef IS_ROOT
  
    mac_root_start();
  
  #endif
  //PROCESS_WAIT_EVENT_UNTIL(ev == loramac_joined);
  //NETSTACK_MAC.on();
  PROCESS_END();
}