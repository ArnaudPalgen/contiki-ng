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
#define LOG_MODULE "LoRa PHY"
#define LOG_LEVEL LOG_LEVEL_INFO
#define LOG_CONF_WITH_COLOR 3
/*---------------------------------------------------------------------------*/
const char* loraphy_params_values[8]={"bw", "cr", "freq", "mod", "pwr", "sf", "wdt", ""};
const char* loraphy_commands_values[5]={"mac pause", "radio set", "radio rx", "radio tx", "sys sleep"};
const char* uart_response[8]={"ok", "invalid_param", "radio_err", "radio_rx", "busy", "radio_tx_ok", "4294967245", "none"};
static bool wait_response = false;
/*---------------------------------------------------------------------------*/
void
loraphy_input(int payload_size)
{
    LOG_DBG("INPUT size %d\n", payload_size);
    int i = LORABUF_UART_RESP_FIRST;
    loraphy_cmd_response_t uart_resp=LORAPHY_CMD_RESPONSE_NONE;
    
    while(i<LORABUF_UART_RESP_FIRST+LORABUF_NUM_EXP_UART_RESP && wait_response){
        uart_resp = (loraphy_cmd_response_t)lorabuf_get_attr(i);
        if(strstr((const char*)lorabuf_c_get_buf(), uart_response[uart_resp])){
            wait_response = false;
            LOG_DBG("UART response: %s\n", uart_response[uart_resp]);
        }
        i++;
    }
    if(uart_resp == LORAPHY_CMD_RESPONSE_RADIO_RX){
        LOG_DBG("data frame\n");
        parse(lorabuf_c_get_buf(), payload_size);
        loramac_input();
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
        lorabuf_clear();
    }
    if(c == '\r'){
        cr = true;
    }else if(c == '\n'){
        if(cr == true){
            loraphy_input((index+1)-LORA_HDR_CHAR_SIZE);
            lorabuf_set_data_c_len(index+1);
            cr = false;
            start = true;
            index = 0;
            LOG_DBG(" \n");
        }
    }else if((int)c != 254 && (int)c != 248 && (int)c != 192 && (int)c != 240){
        LOG_DBG("receive char %c\n ", c);
        lorabuf_c_write_char(c, index);
        index ++;
    }
    return 0;
}
/*---------------------------------------------------------------------------*/
/*write a char* to the UART connection*/
void write_uart(char *s){
    LOG_DBG("write UART %s\n", s);
    while(*s != 0){
        uart_write_byte(LORAPHY_UART_PORT, *s++);
    }
    uart_write_byte(LORAPHY_UART_PORT, '\r');
    uart_write_byte(LORAPHY_UART_PORT, '\n');
}
/*---------------------------------------------------------------------------*/
void
loraphy_init(void)
{
    LOG_DBG("INIT LoRaPHY\n");
    //UART configuration
    uart_init(LORAPHY_UART_PORT);
    uart_set_input(LORAPHY_UART_PORT, &uart_rx);
    loraphy_prepare_data(LORAPHY_CMD_MAC_PAUSE, LORAPHY_PARAM_NONE, "", LORAPHY_CMD_RESPONSE_U_INT, LORAPHY_CMD_RESPONSE_NONE);
}
/*---------------------------------------------------------------------------*/
bool
wait(void)
{
    LOG_DBG("WAIT UART response\n");
    wait_response = true;
    RTIMER_BUSYWAIT_UNTIL(wait_response, RTIMER_SECOND/4);
    return !wait_response;
}
/*---------------------------------------------------------------------------*/
int
loraphy_send(void)
{
    LOG_DBG("SEND %s\n", lorabuf_c_get_buf());
    if(wait_response){
        return 1;
    }
    char* buf = lorabuf_c_get_buf();
    write_uart(buf);
    if (!wait()){
        //todo
        return 1;
    }
    return 0;

}
/*---------------------------------------------------------------------------*/
int
loraphy_prepare_data(loraphy_command_t command, loraphy_param_t parameter, char* value, loraphy_cmd_response_t exp1, loraphy_cmd_response_t exp2)
{
    LOG_DBG("prepare data\n");
    LOG_DBG("   > phy cmd: %d\n", command);
    LOG_DBG("   > phy param: %d\n", parameter);
    LOG_DBG("   > value: %s\n", value);
    LOG_DBG("   > phy resp1: %d\n", exp1);
    LOG_DBG("   > phy resp2: %d\n", exp2);
    if(wait_response){
        LOG_WARN("WAIT a response -> can't send\n");
        return 1;
    }

    const char* cmd = loraphy_commands_values[command];
    const char* param = loraphy_params_values[parameter];
    int size = LORAPHY_PARAM_VALUE_MAX_SIZE + LORAPHY_COMMMAND_VALUE_MAX_SIZE +strlen(value);
    char data[size];
    sprintf(data, "%s %s %s", cmd, param, value);
    lorabuf_set_attr(LORABUF_ATTR_UART_EXP_RESP1, exp1);
    lorabuf_set_attr(LORABUF_ATTR_UART_EXP_RESP2, exp2);
    lorabuf_c_copy_from(data, size);
    return 0;
}
