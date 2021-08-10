#include "rn2483radio.h"
/*---------------------------------------------------------------------------*/
/*macros definition*/

/* root adress*/
#define ROOT_PREFIX 1
#define ROOT_ID 0

/* timeout and rx_time */
#define QUERY_TIMEOUT (CLOCK_SECOND * 10) //10 sec
#define RETRANSMIT_TIMEOUT (CLOCK_SECOND * 5) //3 sec
#define RX_TIME 3000 // 2 sec

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
