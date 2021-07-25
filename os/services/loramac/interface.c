#include "net/ipv6/uip.h"
#include "loramac.h"
#include "sys/log.h"
#include "rn2483radio.h"
#include "dev/button-hal.h"

#define LOG_MODULE "Interface"
#define LOG_LEVEL LOG_LEVEL_DBG
#define LOG_CONF_WITH_COLOR 3

PROCESS(loramac_process, "LoRaMAC-interface");
/*---------------------------------------------------------------------------*/
void lora2ipv6(lora_addr_t *l, uip_ipaddr_t toto)
{
}
/*---------------------------------------------------------------------------*/
void ipv62lora()
{
}
/*---------------------------------------------------------------------------*/
static void
init(void)
{
  LOG_INFO("Init LoRaMAC Interface");
  //loramac_set_input_callback(loramac_input_callback)
  //mac_init();
  process_start(&loramac_process, NULL);
}
/*---------------------------------------------------------------------------*/
static int
output(void)
{ //send data from ipv6 to loramac
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
    init, output};
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(loramac_process, ev, data)
{
  PROCESS_BEGIN();
  LOG_INFO("Welcome !\n");
  PROCESS_WAIT_EVENT_UNTIL(ev == button_hal_press_event);
  LOG_INFO("Button pushed\n");
  mac_init();
  PROCESS_END();
}