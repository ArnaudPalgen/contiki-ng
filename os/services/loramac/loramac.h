#include "rn2483radio.h"
/*---------------------------------------------------------------------------*/
/*macros definition*/

/* root adress*/
#define ROOT_PREFIX 1
#define ROOT_ID 0

/* timeout and rx_time */
//sys/cloh.h defined CLOCK_SECOND as 32
#define QUERY_TIMEOUT (CLOCK_SECOND * 60) //tick
#define RETRANSMIT_TIMEOUT (493) //tick we want an int but the time is 15.400 s so 32*15.4 = 492.8 -> use 493 that is 15.40625 s
#define RX_TIME 15395 // ms

#define MAX_RETRANSMIT 3

#define BUF_SIZE 10

/*---------------------------------------------------------------------------*/

extern process_event_t loramac_network_joined;//the event that is used when the node has joined a LoRaMAC network
extern lora_addr_t loramac_addr;//the node address

/* The different MAC states*/
typedef enum state{
    ALONE, // initial state
    READY, // when node has start RPL
    WAIT_RESPONSE // when the nde wait a response
}state_t;

/*---------------------------------------------------------------------------*/
/* driver functions */

/*Start the LoRaMAC layer*/
void
mac_root_start();

/*Use this function to send data with LoRaMAC*/
int
mac_send_packet(lora_addr_t src_addr, bool need_ack, void* data);

/*set the upper layer callback function that will be used when data are available*/
void
loramac_set_input_callback(void (* listener)(lora_addr_t *src, lora_addr_t *dest, char* data));
