#ifndef LORAMAC_H_
#define LORAMAC_H_

#include "contiki.h"
#include "loraaddr.h"
#include "sys/rtimer.h"
/*---------------------------------------------------------------------------*/
#define LORAMAC_QUERY_TIMEOUT (15*CLOCK_SECOND)
#define LORAMAC_MAX_RETRANSMIT 3
#define LORAMAC_RETRANSMIT_TIMEOUT (CLOCK_SECOND*15) //todo
#define LORAMAC_RETRANSMIT_TIMEOUT_c "15000"//ms
#define LORAMAC_JOIN_SLEEP_TIME_c "30000" // in ms. 5000 ms -> 5s
#define LORAMAC_DISABLE_WDT "0"
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

void loramac_input(void);

void loramac_send(void);

void loramac_print_hdr(lora_frame_hdr_t *hdr);

extern process_event_t loramac_network_joined;//the event that is used when the node has joined a LoRaMAC network
/*---------------------------------------------------------------------------*/
#define LOG_LORA_HDR(level, lora_hdr) do {  \
    if((level) <= (LOG_LEVEL)) { \
        loramac_print_hdr(lora_hdr); \
        printf("\n"); \
    } \
    } while (0)

#define LOG_INFO_LORA_HDR(...)    LOG_LORA_HDR(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_DBG_LORA_HDR(...)    LOG_LORA_HDR(LOG_LEVEL_DBG, __VA_ARGS__)


#endif /* LORAMAC_H_ */
