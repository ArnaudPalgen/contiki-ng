
#ifndef __LORAMAC_CONF_H__
#define __LORAMAC_CONF_H__


#ifndef UART_PORT
#define UART 1
#else
#define UART UART_PORT
#endif

#ifndef LORA_FREQ
#define FREQ "868100000"
#else
#define FREQ LORA_FREQ
#endif

#endif