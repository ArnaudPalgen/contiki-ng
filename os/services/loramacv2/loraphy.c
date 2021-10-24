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
#define LOG_LEVEL LOG_LEVEL_DBG
#define LOG_CONF_WITH_COLOR 3
/*---------------------------------------------------------------------------*/
const char* loraphy_params_values[8]={"bw ", "cr ", "freq ", "mod ", "pwr ", "sf ", "wdt ", ""};
const char* loraphy_commands_values[5]={"mac pause", "radio set ", "radio rx ", "radio tx ", "sys sleep "};
const char* uart_response[8]={"ok", "invalid_param", "radio_err", "radio_rx", "busy", "radio_tx_ok", "4294967245", "none"};
static bool wait_response = false;
/*---------------------------------------------------------------------------*/
void
loraphy_input(unsigned int payload_size)
{
    LOG_DBG("INPUT size %d\n", payload_size);
    int i = LORABUF_UART_RESP_FIRST;
    loraphy_cmd_response_t uart_resp=LORAPHY_CMD_RESPONSE_NONE;
    LOG_DBG("wait_response:{%d}\n", wait_response);
    
    while(i<LORABUF_UART_RESP_FIRST+LORABUF_NUM_EXP_UART_RESP && wait_response){
        LOG_DBG("loop i=%d\n",i);
        uart_resp = (loraphy_cmd_response_t)lorabuf_get_attr(i);
        LOG_DBG("uart resp index: %d\n", uart_resp);
        LOG_DBG("compare %s WITH %s\n", lorabuf_c_get_buf(), uart_response[uart_resp]);
        if(strstr((const char*)lorabuf_c_get_buf(), uart_response[uart_resp])){
            wait_response = false;
            LOG_DBG("%d UART response: %s\n", __LINE__, uart_response[uart_resp]);
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
    LOG_DBG("RECEUVE CGAR\n");
    if(start){
        start=false;
        lorabuf_c_clear();
        //lorabuf_clear();
    }
    if(c == '\r'){
        cr = true;
    }else if(c == '\n'){
        if(cr == true){
            LOG_DBG("lorabuf_c:{%s}\n", lorabuf_c_get_buf());
            loraphy_input((index+1));
            lorabuf_set_data_c_len(index+1);
            cr = false;
            start = true;
            index = 0;
        }
    }else if((int)c != 254 && (int)c != 248 && (int)c != 192 && (int)c != 240){
        LOG_DBG("receive char %c\n ", c);
        //LOG_DBG("index: %d\n", index);
        lorabuf_c_write_char(c, index);
        index ++;
    }
    return 0;
}
/*---------------------------------------------------------------------------*/
/*write a char* to the UART connection*/
void write_uart(char* s, int len){
    LOG_DBG("write UART{%s} with len=%d\n", s, len);
    LOG_DBG("WAIT UART response\n");
    int i=0;
    //int max = lorabuf_get_data_c_len();
    char current;
    while(/**s != 0 && */i < len){
        current = *s++;
        LOG_DBG("send char %s\n", &current);
        uart_write_byte(LORAPHY_UART_PORT, current);
        i++;
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
    LOG_DBG("CALL PHY SEND FOR MAC PAUSE\n");
    loraphy_send();
}
/*---------------------------------------------------------------------------*/
bool
wait(void)
{

    //RTIMER_BUSYWAIT_UNTIL(!wait_response, RTIMER_SECOND);
    while(wait_response){
        LOG_DBG("h1\n");
    }
    return !wait_response;
}
/*---------------------------------------------------------------------------*/
int
loraphy_send(void)
{
    LOG_DBG("ENTER PHY SEND\n");
    if(wait_response){
        return 1;
    }
    char* buf = lorabuf_c_get_buf();
    LOG_DBG("BEFORE WRITE UART\n");
    wait_response = true;
    write_uart(buf, lorabuf_get_data_c_len());
    LOG_DBG("AFTER WRITE UART, BEFORE WAIT %d\n", wait_response);
    //while(wait_response){
    //    //wait_response =
    //}
    //LOG_DBG("LOOP END\n");
    //if (!wait()){
    //    //todo
    //    LOG_DBG("WAIT ERROR\n");
    //    return 1;
    //}
    LOG_DBG("WAIT DONE;-> PHY SEND END\n");
    return 0;

}
/*---------------------------------------------------------------------------*/
int
loraphy_prepare_data(loraphy_command_t command, loraphy_param_t parameter, char* value, loraphy_cmd_response_t exp1, loraphy_cmd_response_t exp2)
{
    LOG_DBG("prepare data\n");
    LOG_DBG("   > phy cmd:{%s}\n", loraphy_commands_values[command]);
    LOG_DBG("   > phy param:{%s}\n", loraphy_params_values[parameter]);
    LOG_DBG("   > value:{%s}\n", value);
    LOG_DBG("   > phy resp1:{%s}\n", uart_response[exp1]);
    LOG_DBG("   > phy resp2:{%s}\n", uart_response[exp2]);
    if(wait_response){
        LOG_WARN("WAIT a response -> can't send\n");
        return 1;
    }

    const char* cmd = loraphy_commands_values[command];
    const char* param = loraphy_params_values[parameter];
    //int size = LORAPHY_PARAM_VALUE_MAX_SIZE + LORAPHY_COMMMAND_VALUE_MAX_SIZE +strlen(value);
    int size = strlen(cmd) + strlen(param) + strlen(value);
    LOG_DBG("SIZE: %d\n", size);
    char data[size];
    sprintf(data, "%s%s%s", cmd, param, value);
    lorabuf_set_attr(LORABUF_ATTR_UART_EXP_RESP1, exp1);
    lorabuf_set_attr(LORABUF_ATTR_UART_EXP_RESP2, exp2);
    lorabuf_c_clear();
    lorabuf_c_copy_from(data, size);
    lorabuf_set_data_c_len(size);
    return 0;
}
