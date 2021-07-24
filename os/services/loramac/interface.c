#include "net/ipv6/uip.h"
#include "loramac.h"
#include "sys/log.h"
#include "rn2483radio.h"

#define LOG_MODULE "Interface"
#define LOG_LEVEL LOG_LEVEL_DBG
#define LOG_CONF_WITH_COLOR 3
/*---------------------------------------------------------------------------*/
void
lora2ipv6(lora_addr_t *l, uip_ipaddr_t)
{

}
/*---------------------------------------------------------------------------*/
void
ipv62lora()
{

}
/*---------------------------------------------------------------------------*/
static void
init(void)
{   
    LOG_INFO("Init LoRaMAC Interface");
    //loramac_set_input_callback(loramac_input_callback)
    mac_init();
}
/*---------------------------------------------------------------------------*/
static int
output(void)
{//send data from ipv6 to loramac
  return 0;
}
/*---------------------------------------------------------------------------*/
static void
loramac_input_callback(lora_addr_t src, lora_addr_t dest, void* data)
{//data from loramac -> ipv6

}
/*---------------------------------------------------------------------------*/
const struct uip_fallback_interface loramac_interface = {
  init, output
};
/*---------------------------------------------------------------------------*/

