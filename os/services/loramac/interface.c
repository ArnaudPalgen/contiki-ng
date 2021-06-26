#include "net/ipv6/uip.h"
#include "loramac.h"
#include "sys/log.h"

#define LOG_MODULE "Interface"
#define LOG_LEVEL LOG_LEVEL_DBG
#define LOG_CONF_WITH_COLOR 3
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
{//send data to loramac
  return 0;
}

/*---------------------------------------------------------------------------*/
const struct uip_fallback_interface loramac_interface = {
  init, output
};
/*---------------------------------------------------------------------------*/