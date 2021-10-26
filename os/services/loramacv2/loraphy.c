#include "loraphy.h"
#include "lorabuf.h"
#include "dev/uart.h"
#include "sys/log.h"
#include "sys/rtimer.h"
#include "framer.h"
#include "loramac.h"
#include "sys/log.h"
#include <stdlib.h>
/*---------------------------------------------------------------------------*/
/* Log configuration */
#define LOG_MODULE "PHY PHY"
#define LOG_LEVEL LOG_LEVEL_INFO
#define LOG_CONF_WITH_COLOR 3
/*---------------------------------------------------------------------------*/
const char* loraphy_params_values[8]={"bw ", "cr ", "freq ", "mod ", "pwr ", "sf ", "wdt ", ""};
const char* loraphy_commands_values[5]={"mac pause", "radio set ", "radio rx ", "radio tx ", "sys sleep "};
const char* uart_response[8]={"ok", "invalid_param", "radio_err", "radio_rx", "busy", "radio_tx_ok", "4294967245", "none"};
static bool ready = true;
static void (* c)( loraphy_sent_status_t status) = NULL;
/*---------------------------------------------------------------------------*/

void
set_notify_state(bool is_ready)
{
    LOG_DBG("set ready to %s\n", ready ? "true":"false");
    ready = is_ready;
    if(is_ready && c != NULL){
        c(LORAPHY_SENT_DONE);
    }
}
/*---------------------------------------------------------------------------*/
void
loraphy_input()
{
    LOG_INFO("UART RX\n");
    LOG_INFO("   > buf:{%s}\n", lorabuf_c_get_buf());
    LOG_INFO("   > buf size:{%d}\n", lorabuf_get_data_c_len());

    int i = LORABUF_UART_RESP_FIRST;
    loraphy_cmd_response_t uart_resp=LORAPHY_CMD_RESPONSE_NONE;

    while(i<LORABUF_UART_RESP_FIRST+LORABUF_NUM_EXP_UART_RESP && !ready){
        uart_resp = (loraphy_cmd_response_t)lorabuf_get_attr(i);
        LOG_DBG("compare {%s} WITH {%s}\n", lorabuf_c_get_buf(), uart_response[uart_resp]);
        if(strstr((const char*)lorabuf_c_get_buf(), uart_response[uart_resp])){
            LOG_INFO("expected response\n");
            set_notify_state(true);
        }
        i++;
        if(i==LORABUF_UART_RESP_FIRST+LORABUF_NUM_EXP_UART_RESP && ready==false){
            LOG_INFO("response is not the expected\n");
        }
    }

    if(uart_resp == LORAPHY_CMD_RESPONSE_RADIO_RX){
        LOG_DBG("UART resp is data frame\n");
        parse(lorabuf_c_get_buf(), lorabuf_get_data_c_len());
        loramac_input();
    }else{
        LOG_DBG("UART resp is NOT data frame\n");
    }
}
/*---------------------------------------------------------------------------*/
int
uart_rx(unsigned char c)
{
    static bool cr = false;
    static unsigned int index = 0;
    static bool start = true;

    if(start){
        start=false;
        lorabuf_c_clear();
    }
    if(c == '\r'){
        cr = true;
    }else if(c == '\n'){
        if(cr == true){
            lorabuf_set_data_c_len(index);
            loraphy_input();
            cr = false;
            start = true;
            index = 0;
        }
    }else if((int)c != 254 && (int)c != 248 && (int)c != 192 && (int)c != 240){
        lorabuf_c_write_char(c, index);
        index ++;
    }
    return 0;
}
/*---------------------------------------------------------------------------*/
/*write a char* to the UART connection*/
void
write_uart(char *s, int len)
{
    LOG_INFO("write UART{%s} with len=%d\n", s, len);

    for(int i=0;i<len;i++){
        uart_write_byte(LORAPHY_UART_PORT, *s++);
    }

    uart_write_byte(LORAPHY_UART_PORT, '\r');
    uart_write_byte(LORAPHY_UART_PORT, '\n');
}
/*---------------------------------------------------------------------------*/
void
loraphy_init(void)
{
    LOG_INFO("INIT LoRaPHY\n");

    /* UART configuration */
    uart_init(LORAPHY_UART_PORT);
    uart_set_input(LORAPHY_UART_PORT, &uart_rx);

    /*SEND mac pause*/
    loraphy_prepare_data(LORAPHY_CMD_MAC_PAUSE, LORAPHY_PARAM_NONE, "", -1,LORAPHY_CMD_RESPONSE_U_INT, LORAPHY_CMD_RESPONSE_NONE);
    loraphy_send();
    RTIMER_BUSYWAIT_UNTIL(ready, RTIMER_SECOND/4);
}
/*---------------------------------------------------------------------------*/
void
loraphy_set_callback(void (* callback)( loraphy_sent_status_t status))
{
    c = callback;
}
/*---------------------------------------------------------------------------*/
int
loraphy_send(void)
{
    LOG_DBG("phy send\n");
    LOG_DBG("   > buf:{%s}\n", lorabuf_c_get_buf());
    LOG_DBG("   > buf size:{%d}\n", lorabuf_get_data_c_len());
    if(!ready){
        LOG_WARN("impossible to send because a response is expected\n");
        return 1;
    }
    char* buf = lorabuf_c_get_buf();
    ready = false;
    write_uart(buf, lorabuf_get_data_c_len());
    LOG_DBG("frame sended\n");
    return 0;
}
/*---------------------------------------------------------------------------*/
int
loraphy_prepare_data(loraphy_command_t command, loraphy_param_t parameter, char* value, int16_t len,loraphy_cmd_response_t exp1, loraphy_cmd_response_t exp2)
{
    LOG_DBG("prepare data\n");
    LOG_DBG("   > phy cmd:{%s}\n", loraphy_commands_values[command]);
    LOG_DBG("   > phy param:{%s}\n", loraphy_params_values[parameter]);
    LOG_DBG("   > value:{%s}\n", value);
    LOG_DBG("   > phy resp1:{%s}\n", uart_response[exp1]);
    LOG_DBG("   > phy resp2:{%s}\n", uart_response[exp2]);

    const char* cmd = loraphy_commands_values[command];
    const char* param = loraphy_params_values[parameter];
    unsigned int size = strlen(cmd) + strlen(param) + ((len > -1) ? len:strlen(value));
    char data[size];
    sprintf(data, "%s%s%s", cmd, param, value);
    lorabuf_set_attr(LORABUF_ATTR_UART_EXP_RESP1, exp1);
    lorabuf_set_attr(LORABUF_ATTR_UART_EXP_RESP2, exp2);
    lorabuf_c_clear();
    lorabuf_c_copy_from(data, size);
    LOG_WARN("------------------- DATALEN TO %d\n", size);
    lorabuf_set_data_c_len(size);
    return 0;
}
