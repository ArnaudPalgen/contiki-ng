#include "contiki.h"

#ifndef LORAPHY_H_
#define LORAPHY_H_

#define LORAPHY_UART_PORT 1

#define LORAPHY_PARAM_VALUE_MAX_SIZE 4
#define LORAPHY_COMMMAND_VALUE_MAX_SIZE 10

/*---------------------------------------------------------------------------*/
typedef enum loraphy_param{
    LORAPHY_PARAM_MODE,
    LORAPHY_PARAM_FREQ,
    LORAPHY_PARAM_BW,
    LORAPHY_PARAM_CR,
    LORAPHY_PARAM_PWR,
    LORAPHY_PARAM_SF
}loraphy_param_t;

typedef enum loraphy_command{
    LORAPHY_MAC_PAUSE,
    LORAPHY_RADIO_SET,
    LORAPHY_RADIO_RX,
    LORAPHY_RADIO_TX,
    LORAPHY_SYS_SLEEP
}loraphy_command_t;

typedef enum loraphy_cmd_response{
    LORAPHY_CMD_RESPONSE_OK,
    LORAPHY_CMD_RESPONSE_INVALID_PARAM,
    LORAPHY_CMD_RESPONSE_RADIO_ERR,
    LORAPHY_CMD_RESPONSE_RADIO_RX,
    LORAPHY_CMD_RESPONSE_BUSY,
    LORAPHY_CMD_RESPONSE_RADIO_TX_OK,
    LORAPHY_CMD_RESPONSE_U_INT,
    LORAPHY_CMD_RESPONSE_NONE
}loraphy_cmd_response_t;
/*---------------------------------------------------------------------------*/

//set a parameter (loraphy_param_t) put info in lorabuf_c and call send
int loraphy_set_parameter(loraphy_param_t type, char* value);

//send data in lorabuf_c
int loraphy_send(void);

//init loraphy
void loraphy_init(void);

#endif /* LORAPHY_H_ */