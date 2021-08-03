#include "net/ipv6/uip.h"
#include "net/netstack.h"
#include "loramac.h"
#include "sys/log.h"
//#include "rn2483radio.h"
#include "dev/button-hal.h"

#define LOG_MODULE "Interface"
#define LOG_LEVEL LOG_LEVEL_DBG
#define LOG_CONF_WITH_COLOR 3

PROCESS(loramac_process, "LoRaMAC-interface");
/*
typedef union uip_ip6addr_t {
  uint8_t  u8[16];                      //Initializer, must come first.
  uint16_t u16[8];
} uip_ip6addr_t;

typedef uip_ip6addr_t uip_ipaddr_t;
*/
/*
|<-----1----->|<--5-->|<-----1----->|<--1-->|<----------6---------->|<---2--->|
| IPv6 PREFIX | ZEROS | LORA PREFIX | ZEROS | COMMON_LINK_ADDR_PART | NODE_ID |
 0           0 1     5 6           6 7     7 8                    13 14     15

 typedef struct lora_addr{
    uint8_t prefix;
    uint16_t id;
}lora_addr_t;

IPv6_PREFIX = "FD00"
COMMON_LINK_ADDR_PART = "5F756D6F6E73"
*/
/*---------------------------------------------------------------------------*/
void lora2ipv6(lora_addr_t *src_addr, uip_ip6addr_t *dest_addr)
{
  uip_ip6addr_u8(dest_addr, 0xFD, 0, 0, 0, 0, 0, src_addr->prefix, 0, '_','u','m','o','n','s', src_addr->id>>8, src_addr->id);

}
/*---------------------------------------------------------------------------*/
void ipv62lora(uip_ip6addr_t *src_addr, lora_addr_t *dest_addr)
{
  dest_addr->prefix = src_addr->u8[6];
  dest_addr->id = src_addr->u8[14] + ((src_addr->u8[15]) << 8);
}
/*---------------------------------------------------------------------------*/
static void
init(void)
{
  
  //loramac_set_input_callback(loramac_input_callback)
  //mac_init();
  LOG_INFO("Welcome to LoRaMAC interface !\n");
  #ifdef IS_ROOT
    LOG_INFO("Init LoRaMAC Interface\n");
    process_start(&loramac_process, NULL);
  #else
    LOG_INFO("Not the root -> Do nothing for LoRaMAC\n");
  #endif
}
/*---------------------------------------------------------------------------*/
static int
output(void)
{ //send data from ipv6 to loramac
  //mac_send_packet(lora_addr_t src_addr, bool need_ack, void* data)
  //uip_ip6addr_t
  //UIP_BUFSIZE
  //todo definir UIP_CONF_BUFFER_SIZE à 279: 247 du payload loramac + les deux addresses ipv6
  //todo (src et dest ) pas utilisées
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
  //UIP_IP_BUF->srcipaddr;
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
  LOG_INFO("NetStack OFF\n");
  PROCESS_WAIT_EVENT_UNTIL(ev == button_hal_press_event);
  LOG_INFO("Button pushed\n");
  mac_init();
  //PROCESS_WAIT_EVENT_UNTIL(ev == loramac_joined);
  //NETSTACK_MAC.on();
  LOG_INFO("NetStack ON\n");
  PROCESS_END();
}