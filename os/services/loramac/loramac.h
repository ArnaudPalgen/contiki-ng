#include "rn2483radio.h"
/*---------------------------------------------------------------------------*/
//ROOT ADDR
#define ROOT_PREFIX 1
#define ROOT_ID 0

//timeout and rx_time
#define QUERY_TIMEOUT (CLOCK_SECOND * 10) //10 sec
#define RETRANSMIT_TIMEOUT (CLOCK_SECOND * 5) //3 sec
#define RX_TIME 3000 // 2 sec

#define MAX_RETRANSMIT 3

#define BUF_SIZE 10

//#define IS_ROOT 1

/*---------------------------------------------------------------------------*/
//static process_event_t loramac_joined;


typedef enum state{
    ALONE, //initial state
    //JOINED, // when node has received the prefix
    READY, // when node has start RPL
    WAIT_RESPONSE
}state_t;

void mac_init();

int
mac_send_packet(lora_addr_t src_addr, bool need_ack, void* data);