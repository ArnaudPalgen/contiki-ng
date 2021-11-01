#ifndef LORAPHY_H_
#define LORAPHY_H_

#include "contiki.h"
#include "loramac-conf.h"

//#define LORAPHY_UART_PORT 1

#define LORAPHY_PARAM_VALUE_MAX_SIZE 4
#define LORAPHY_COMMMAND_VALUE_MAX_SIZE 10

#define LORAPHY_NUM_RADIO_PARAM 6

/*---------------------------------------------------------------------------*/
typedef enum loraphy_sent_status{
    LORAPHY_SENT_DONE,
    LORAPHY_INPUT_DATA,
}loraphy_sent_status_t;
typedef enum loraphy_param{
    LORAPHY_PARAM_BW,
    LORAPHY_PARAM_CR,
    LORAPHY_PARAM_FREQ,
    LORAPHY_PARAM_MODE,
    LORAPHY_PARAM_PWR,
    LORAPHY_PARAM_SF,
    LORAPHY_PARAM_WDT,
    LORAPHY_PARAM_NONE,
}loraphy_param_t;

typedef enum loraphy_command{
    LORAPHY_CMD_MAC_PAUSE,
    LORAPHY_CMD_RADIO_SET,
    LORAPHY_CMD_RADIO_RX,
    LORAPHY_CMD_RADIO_TX,
    LORAPHY_CMD_SYS_SLEEP
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

//init loraphy
void loraphy_init(void);

//send data in lorabuf_c
int loraphy_send(void);

int loraphy_prepare_data(loraphy_command_t command, loraphy_param_t parameter, char* value, int16_t len, loraphy_cmd_response_t exp1, loraphy_cmd_response_t exp2);
void loraphy_set_callback(void (* callback)( loraphy_sent_status_t status));
/*---------------------------------------------------------------------------*/
#define LORAPHY_TX(data, len){\
    loraphy_prepare_data(LORAPHY_CMD_RADIO_TX, LORAPHY_PARAM_NONE, data, len, LORAPHY_CMD_RESPONSE_RADIO_TX_OK, LORAPHY_CMD_RESPONSE_RADIO_ERR);\
    loraphy_send();\
}

#define LORAPHY_SET_PARAM(param, value){\
    loraphy_prepare_data(LORAPHY_CMD_RADIO_SET, param, value, -1,LORAPHY_CMD_RESPONSE_OK, LORAPHY_CMD_RESPONSE_NONE);\
    loraphy_send();\
}

#define LORAPHY_RX() ({  \
    loraphy_prepare_data(LORAPHY_CMD_RADIO_RX, LORAPHY_PARAM_NONE, "0", -1, LORAPHY_CMD_RESPONSE_OK, LORAPHY_CMD_RESPONSE_NONE);  \
    loraphy_send();  \
    })

#define LORAPHY_SLEEP(duration) ({  \
    loraphy_prepare_data(LORAPHY_CMD_SYS_SLEEP, LORAPHY_PARAM_NONE, duration, -1, LORAPHY_CMD_RESPONSE_OK, LORAPHY_CMD_RESPONSE_NONE);  \
    loraphy_send();  \
    })

#endif /* LORAPHY_H_ */
