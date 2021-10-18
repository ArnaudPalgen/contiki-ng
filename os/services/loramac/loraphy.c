#include "loraphy.h"
#include "lorabuf.h"
#include "dev/uart.h"
#include "sys/log.h"
#include "sys/rtimer.h"
#include "framer.h"
#include "loramac.h"
#include <stdlib.h>
/*---------------------------------------------------------------------------*/
const char* loraphy_params_values[8]={"bw", "cr", "freq", "mod", "pwr", "sf", "wdt", ""};
const char* loraphy_commands_values[5]={"mac pause", "radio set", "radio rx", "radio tx", "sys sleep"};
const char* uart_response[8]={"ok", "invalid_param", "radio_err", "radio_rx", "busy", "radio_tx_ok", "4294967245", "none"};
static wait_response = false;
/*---------------------------------------------------------------------------*/
void
loraphy_input(int payload_size)
{
    int i = LORABUF_UART_RESP_FIRST;
    loraphy_cmd_response_t uart_resp;
    
    while(i<LORABUF_UART_RESP_FIRST+LORABUF_NUM_EXP_UART_RESP && wait_response){
        uart_resp = (loraphy_cmd_response_t)lorabuf_get_attr(i);
        if(strstr((const char*)lorabuf_c_get_buf(), uart_response[uart_resp])){
            wait_response = false;
        }
        i++;
    }
    if(uart_response[uart_resp] == LORAPHY_CMD_RESPONSE_RADIO_RX){
        parse(lorabuf_c_get_buf, payload_size);
        loramac_input();
    }
}
/*---------------------------------------------------------------------------*/
int
uart_rx(unsigned char c)
{
    static bool cr = false;
    static unsigned int index = 0;

    if(c == '\r'){
        cr = true;
    }else if(c == '\n'){
        if(cr == true){
            loraphy_input(index+1-LORA_HDR_CHAR_SIZE);
            cr = false;
            index = 0;
        }
    }else if((int)c != 254 && (int)c != 248 && (int)c != 192 && (int)c != 240){
        lorabuf_c_write_char(c, index);
        index ++;
    }
}
/*---------------------------------------------------------------------------*/
/*write a char* to the UART connection*/
void write_uart(char *s){
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
    //UART configuration
    uart_init(LORAPHY_UART_PORT);
    uart_set_input(LORAPHY_UART_PORT, &uart_rx);
    loraphy_prepare_data(LORAPHY_CMD_MAC_PAUSE, LORAPHY_PARAM_NONE, "", LORAPHY_CMD_RESPONSE_U_INT, LORAPHY_CMD_RESPONSE_NONE);
}
/*---------------------------------------------------------------------------*/
bool
wait(void)
{   
    wait_response = true;
    RTIMER_BUSYWAIT_UNTIL(wait_response, RTIMER_SECOND/4);
    return !wait_response;
}
/*---------------------------------------------------------------------------*/
int
loraphy_send(void)
{
    if(wait_response){
        return 1;
    }
    char* buf = lorabuf_c_get_buf();
    write_uart(buf);
    if (!wait()){
        //todo log error 
        return 1;
    }
    return 0;

}
/*---------------------------------------------------------------------------*/
int
loraphy_prepare_data(loraphy_command_t command, loraphy_param_t parameter, char* value, loraphy_cmd_response_t exp1, loraphy_cmd_response_t exp2)
{
    if(wait_response){
        return 1;
    }

    char* cmd = loraphy_commands_values[command];
    char* param = loraphy_params_values[parameter];
    int size = LORAPHY_PARAM_VALUE_MAX_SIZE + LORAPHY_COMMMAND_VALUE_MAX_SIZE +strlen(value);
    char data[size];
    sprintf(data, "%s %s %s", cmd, param, value);
    lorabuf_set_attr(LORABUF_ATTR_UART_EXP_RESP1, exp1);
    lorabuf_set_attr(LORABUF_ATTR_UART_EXP_RESP2, exp2);
    lorabuf_c_copy_from(data, size);
    return 0;
}
