#include "contiki.h"

#include "rn2483radio.h"
#include "dev/uart.h"
#include "sys/log.h"
#include "sys/mutex.h"

#include <stdlib.h>

/*---------------------------------------------------------------------------*/
//log configuration
#define LOG_MODULE "LoRa PHY"
#define LOG_LEVEL LOG_LEVEL_DBG
#define LOG_CONF_WITH_COLOR 3

//TX buffer initialization
#define TX_BUF_SIZE 20
static uart_frame_t tx_buffer [TX_BUF_SIZE];
static uint8_t w_i = 0;// index to write in the buffer
static uint8_t r_i = 0;// index to read in the buffer 
static uint8_t tx_buf_size = 0;// size of the buffer

//Function to send data to. i.e. data from ratio_rx
static int (* handler)( lora_frame_t frame) = NULL;

//Array that contains the expected responses for a sent UART command
//The expected responses, when received, allow the next UART command to be sent
static uart_response_t expected_response[UART_EXP_RESP_SIZE];
const char* uart_response[8]={"ok", "invalid_param", "radio_err", "radio_rx", "busy", "radio_tx_ok", "4294967245", "none"};

//commands that can be sent
//see uart_command_t
const char* uart_command[11]={"mac pause", "radio set mod ", "radio set freq ", "radio set wdt ", "radio rx ", "radio tx ", "sys sleep ", "radio set bw ", "radio set cr ", "radio set pwr ", "radio set sf "};


mutex_t response_mutex;//mutex for expected_response
mutex_t tx_buf_mutex;// mutex for tx_buffer

static process_event_t new_tx_frame_event;//event that signals to the TX process that a new frame is available to be sent 
static process_event_t can_send_event;//event that signals to the TX process that the correct UART response was received

static bool can_send = true;

PROCESS(ph_rx, "LoRa-PHY rx process");
PROCESS(ph_tx, "LoRa-PHY tx process");


/*---------------------------------------------------------------------------*/
/*print functions*/

void print_uart_frame(uart_frame_t *frame){
    printf("{ cmd:%s ", uart_command[frame->cmd]);
    printf("resp: [");
    uart_response_t *f_expected_response = frame->expected_response;
    for(int i=0;i<UART_EXP_RESP_SIZE-1;i++){
        printf("%s, ",uart_response[f_expected_response[i]]);
    }
    printf("%s] }\n",uart_response[f_expected_response[UART_EXP_RESP_SIZE-1]]);
    
}

void print_lora_addr(lora_addr_t *addr){
    printf("%d:%d", addr->prefix, addr->id);
}

void print_lora_frame(lora_frame_t *frame){
    printf("{src:");
    print_lora_addr(&(frame->src_addr));
    printf(" dest:");
    print_lora_addr(&(frame->dest_addr));
    printf(" k:%d seq:%d next:%d cmd:%d data:%s\n", frame->k, frame->seq,\
        frame->next, frame->command, frame->payload);
}

/*---------------------------------------------------------------------------*/
/* build lora_frame_t from char* */
int parse(lora_frame_t *dest, char *data){

    if(strlen(data) < HEADER_SIZE){
        return 1;
    }
    
    lora_frame_t result;
    
    char prefix_c[2];
    char id_c[4];
    
    uint8_t prefix;
    uint16_t id;
    lora_addr_t src_addr;
    lora_addr_t dest_addr;

    /*extract src addr*/
    memcpy(prefix_c, data, 2);
    data = data+2;
    memcpy(id_c, data, 4);
    data = data+4;

    prefix = (uint8_t) strtol(prefix_c, NULL, 16);
    id = (uint16_t) strtol(id_c, NULL, 16);

    src_addr.prefix = prefix;
    src_addr.id=id; 
    result.src_addr = src_addr;

    /*extract dest addr*/
    memcpy(prefix_c, data, 2);
    data = data+2;
    memcpy(id_c, data, 4);
    data = data+4;

    prefix = (uint8_t) strtol(prefix_c, NULL, 16);
    id = (uint16_t) strtol(id_c, NULL, 16);

    dest_addr.prefix = prefix;
    dest_addr.id = id;
    result.dest_addr = dest_addr;

    /*extact flags and command*/
    char cmd[2];
    memcpy(cmd, data, 2);
    data = data+2;
    uint8_t i_cmd = (uint8_t)strtol(cmd, NULL, 16);

    uint8_t flag_filter = 0x01;
    uint8_t command_filter = 0x0F;

    bool k    = (bool)((i_cmd >> 7) & flag_filter);
    bool next = (bool)((i_cmd >> 6) & flag_filter);
    
    mac_command_t command = (uint8_t)( i_cmd & command_filter );
    
    result.k = k;
    result.command = command;
    result.next = next;

    /* extract SN */
    char sn_c[2];
    memcpy(sn_c, data, 2);
    data = data+2;
    uint8_t sn = (uint8_t)strtol(sn_c, NULL, 16);
    result.seq = sn;

    /*extract payload*/
    result.payload = data;
    memcpy(dest, &result, sizeof(lora_frame_t));
    
    return 0;
}

/*---------------------------------------------------------------------------*/
/*convert lora_frame_t to hex*/
int to_frame(lora_frame_t *frame, char *dest){

    char result[HEADER_SIZE+PAYLOAD_MAX_SIZE]="";

    /*create src and dest addr*/
    char src_addr[6];
    char dest_addr[6];
    
    sprintf(src_addr, "%02X%04X", frame->src_addr.prefix, frame->src_addr.id);
    sprintf(dest_addr, "%02X%04X", frame->dest_addr.prefix, frame->dest_addr.id);
    
    /*create flags and MAC command*/
    char flags_command[2];

    uint8_t f_c = 0;
    if(frame->k){
        f_c = f_c | K_FLAG;
    }
    if(frame->next){
        f_c = f_c | NEXT_FLAG;
    }
    f_c = f_c | ((uint8_t) frame->command);

    sprintf(flags_command, "%02X", f_c);

    /* create SN */
    char sn[2];
    sprintf(sn, "%02X", frame->seq);
    
    /* concat all computed values to result */
    strcat(result, src_addr);
    strcat(result, dest_addr);
    strcat(result, flags_command);
    strcat(result, sn);
    
    /* create payload */
    int payload_size = 0;
    if(frame->payload != NULL){
        payload_size = strlen(frame->payload);
    } 
    
    if(payload_size>0){
        if(payload_size%2 != 0){
            strcat(result, "0");
        }
        strcat(result, frame->payload);
    }
    
    /*copy result to dest */
    memcpy(dest, &result, HEADER_SIZE+payload_size+1);
    return 0;
}

/*---------------------------------------------------------------------------*/
/*write a char* to the UART connection*/
void write_uart(char *s){
    LOG_INFO("PHY TX:%s\n", s);
    while(*s != 0){
        uart_write_byte(RN2483_UART_PORT, *s++);
    }
    uart_write_byte(RN2483_UART_PORT, '\r');
    uart_write_byte(RN2483_UART_PORT, '\n');
}

/*---------------------------------------------------------------------------*/
void phy_register_listener(int (* listener)(lora_frame_t frame)){
    handler = listener;
}

/*---------------------------------------------------------------------------*/
/*Process an incomming UART command*/
void process_command(unsigned char *command){
    LOG_DBG("UART response:%s\n", command);

    lora_frame_t frame;

    while(!mutex_try_lock(&response_mutex)){}
    LOG_DBG("compare response with:\n");
    for(int i=0;i<UART_EXP_RESP_SIZE;i++){
        LOG_DBG("   - %s\n",uart_response[expected_response[i]]);
        if(strstr((const char*)command, uart_response[expected_response[i]]) != NULL){
            /*the UART response is the expected response*/
            LOG_DBG("this is th expected response\n");
            if(expected_response[i] == UART_RADIO_RX && parse(&frame, (char*)(command+10))==0){
                /*receive data -> transmit to MAC layer*/
                LOG_INFO("PHY RX:%s\n", (char*)(command+10));
                handler(frame);
            }
            /* signal to the tx process that the next frame can be sent */
            process_post(&ph_tx, can_send_event, NULL);
            break;
        }
    }
    mutex_unlock(&response_mutex);

}

/*---------------------------------------------------------------------------*/
/* Callback function that receive bytes from UART*/
int uart_rx(unsigned char c){
    static unsigned char buf [FRAME_SIZE];
    static unsigned short index = 0;
    static bool cr = false;

    if(c == '\r'){
      cr = true;
    }else if(c == '\n'){
      if(cr==true){
        process_command(buf);
        index = 0;
        cr = false;
        memset(buf, 0, FRAME_SIZE*sizeof(char));
      }
    }
    if((int)c != 254 && (int)c != 248 && c!='\n' && (int)c != 192 && (int)c != 240 && c!='\r'){
        buf[index++] = c;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/*Append an UART frame to the TX buffer.*/
void uart_tx(uart_frame_t uart_frame){

    LOG_DBG("want to add frame to tx buffer: ");
    LOG_DBG_UFRAME(&uart_frame);

    while(!mutex_try_lock(&tx_buf_mutex)){}
    if(tx_buf_size < TX_BUF_SIZE){
        tx_buffer[w_i] = uart_frame;
        tx_buf_size ++;
        w_i = (w_i+1)%TX_BUF_SIZE;
        LOG_DBG("frame added\n");
        LOG_DBG("TX BUF SIZE: %d\n", tx_buf_size);
    }else{
        LOG_WARN("TX BUF FULL\n");
        LOG_WARN("TX BUF SIZE: %d\n", tx_buf_size);
    }
    mutex_unlock(&tx_buf_mutex);
    process_post(&ph_tx, new_tx_frame_event, NULL);
}
/*---------------------------------------------------------------------------*/
void phy_init(){
    //add config command to tx_buf
    //send event to process
    LOG_INFO("Init LoRa PHY\n");

    //create events
    new_tx_frame_event = process_alloc_event();
    can_send_event = process_alloc_event();
    
    //start process
    process_start(&ph_rx, NULL);
    process_start(&ph_tx, NULL);

    //send initialisation UART commands
    uart_frame_t mac_pause = {MAC_PAUSE, STR, {.s=""}, {UART_U_INT, UART_NONE}};
    uart_frame_t set_mode = {SET_MOD, STR, {.s=LORA_RADIO_MODE},{UART_OK, UART_NONE}};
    uart_frame_t set_freq = {SET_FREQ, INT, {.d=LORA_RADIO_FREQ}, {UART_OK, UART_NONE}};
    uart_frame_t set_bw = {SET_BW, INT, {.d=LORA_RADIO_BW},{UART_OK, UART_NONE}};
    uart_frame_t set_cr = {SET_CR, STR, {.s=LORA_RADIO_CR}, {UART_OK, UART_NONE}};
    uart_frame_t set_pwr = {SET_PWR, INT, {.d=LORA_RADIO_PWR}, {UART_OK, UART_NONE}};
    uart_frame_t set_sf = {SET_SF, STR, {.s=LORA_RADIO_SF},{UART_OK, UART_NONE}};

    uart_tx(mac_pause);
    uart_tx(set_mode);
    uart_tx(set_freq);
    uart_tx(set_bw);
    uart_tx(set_cr);
    uart_tx(set_pwr);
    uart_tx(set_sf);

    LOG_DBG("Initialization completed\n");

}

/*---------------------------------------------------------------------------*/
int phy_tx(lora_frame_t frame){
    uart_frame_t uart_frame = {
        TX,
        LORA,
        {.lora_frame = frame},
        {UART_RADIO_TX_OK,UART_RADIO_ERR}
    };
    uart_tx(uart_frame);
    return 0;

}

int phy_timeout(int timeout){
    if(timeout < 0 || timeout > 4294967295 ){
        LOG_WARN("PHY timeout parameter incorrect\n");
        return 1;
    }

    uart_frame_t uart_frame = {
        SET_WDT,
        INT,
        {.d = timeout},
        {UART_OK, UART_NONE}
    };
    uart_tx(uart_frame);
    return 0;
}

int phy_sleep(int duration){
    if(duration < 100 || duration > 4294967296 ){
        LOG_WARN("PHY sleep parameter incorrect\n");
        return 1;
    }

    uart_frame_t uart_frame = {
        SLEEP,
        INT,
        {.d = duration},
        {UART_OK, UART_NONE}
    };
    uart_tx(uart_frame);
    return 0;
}

int phy_rx(){
    uart_frame_t uart_frame = {
        RX,
        STR,
        {.s = "0"},
        {UART_RADIO_ERR, UART_RADIO_RX}
    };
    uart_tx(uart_frame);
    return 0;
}

/*---------------------------------------------------------------------------*/
/*
 * RX process.
 * A process_thread is used to avoid interruption. When a UART frame is received and decoded,
 * if it is the expected reponse, process_command(unsigned char *command) send an event to
 * the TX process to send the next UART frame.
 *
*/
PROCESS_THREAD(ph_rx, ev, data){
    PROCESS_BEGIN();
    
    //UART configuration
    uart_init(RN2483_UART_PORT);
    uart_set_input(RN2483_UART_PORT, &uart_rx);
    
    PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/*Return true if the TX buffer is empty. false otherwise.*/
bool buf_empty(){
    bool result;
    while(!mutex_try_lock(&tx_buf_mutex)){}
    result = tx_buf_size == 0;
    mutex_unlock(&tx_buf_mutex);
    return result;
}

/*---------------------------------------------------------------------------*/
/*The TX process that sends frames in the TX buffer to the UART connection.*/
PROCESS_THREAD(ph_tx, ev, data){
    
    uart_frame_t uart_frame; // The UART frame to send
    bool buf_empty_var; // true if the buffer is empty, false otherwise.
    
    PROCESS_BEGIN();

    while (true){
        buf_empty_var = buf_empty();
        if(buf_empty_var){
            // The buffer is empty. Wait an incomming frame.
            PROCESS_WAIT_EVENT_UNTIL(ev==new_tx_frame_event);
        }
        
        if(!can_send){//to be sure. but should never happen.
            continue;
        }
        
        //acquire mutex for buffer
        while(!mutex_try_lock(&tx_buf_mutex)){}
        
        //get next uart frame to send
        uart_frame = tx_buffer[r_i];
        tx_buf_size --;
        r_i = (r_i+1)%TX_BUF_SIZE;
        
        LOG_DBG("read frame from tx buffer: ");
        LOG_DBG_UFRAME(&uart_frame);
        LOG_DBG("TX BUF SIZE: %d\n", tx_buf_size);
        
        mutex_unlock(&tx_buf_mutex);
        
        //acquire mutex for expected_response
        while(!mutex_try_lock(&response_mutex)){}
        
        //update expected response
        for(int i=0;i<UART_EXP_RESP_SIZE;i++){
            expected_response[i] = uart_frame.expected_response[i];
        }
        mutex_unlock(&response_mutex);

        can_send = false;
        
        /* Serialize the uart_frame to a char array that will be sent. */
        char result[FRAME_SIZE]="";
        if(uart_frame.type == STR){ // The uart_frame contains a String
            sprintf(result, "%s%s", uart_command[uart_frame.cmd], uart_frame.data.s);

        }else if(uart_frame.type == LORA){ // The uart_frame contains a lora_frame    
            
            strcat(result, uart_command[uart_frame.cmd]);
            lora_frame_t frame = uart_frame.data.lora_frame;
            to_frame(&frame, result+strlen(result));
            
        }else if(uart_frame.type == INT){ // The uart_frame contains an integer 
            sprintf(result, "%s%d", uart_command[uart_frame.cmd], uart_frame.data.d);
        }


        write_uart(result);// write the serialized uart_frame to UART 
        
        /* Wait until can send the next item in the buffer. i.e. the expected reponse has be received. */
        LOG_DBG("wait to send next frame ....\n");
        while(!can_send){
            PROCESS_WAIT_EVENT();
            if(ev == can_send_event){
                can_send = true;
            }
        }
        LOG_DBG("can send next frame\n");
    }

    PROCESS_END();
}
