#ifndef LORAMAC_H_
#define LORAMAC_H_

#include "contiki.h"
#include "loraaddr.h"
/*---------------------------------------------------------------------------*/
#define LORAMAC_QUERY_TIMEOUT 1 //todo
#define LORAMAC_MAX_RETRANSMIT 3
#define LORAMAC_RETRANSMIT_TIMEOUT 1 //todo
#define LORAMAC_JOIN_SLEEP_TIME 5000 // in ms. 5000 ms -> 5s
/*---------------------------------------------------------------------------*/
/*The supported LoRaMAC commands*/
typedef enum loramac_command {
    JOIN,
    JOIN_RESPONSE,
    DATA,
    ACK,
    QUERY,
}loramac_command_t;

/*The different MAC states*/
typedef enum loramac_state{
    ALONE, // initial state
    READY, // when node has start RPL
    WAIT_RESPONSE // when the node wait a response
}loramac_state_t;

typedef struct lora_frame_hdr{
    bool confirmed; // true if the frame need an ack in return, false otherwise
    uint8_t seqno; // The sequence number of the frame
    bool next; // true if another frame follow this frame. Only for downward traffic
    loramac_command_t command; // The MAC command of the frame
    lora_addr_t src_addr; // The source Address
    lora_addr_t dest_addr; // The destination Address
}lora_frame_hdr_t;
/*---------------------------------------------------------------------------*/
/*Start the LoRaMAC layer*/
void loramac_root_start(void);

int loramac_input(void);

void loramac_send(void);

#endif /* LORAMAC_H_ */
