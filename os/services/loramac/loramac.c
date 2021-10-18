#include "sys/log.h"
#include "loramac.h"
#include "loraaddr.h"
#include "lorabuf.h"
#include "framer.h"
#include "loraphy.h"
#include "sys/node-id.h"
#include "lorabridge.h"
/*---------------------------------------------------------------------------*/
/* Log configuration */
#define LOG_MODULE "LoRa MAC"
#define LOG_LEVEL LOG_LEVEL_INFO
#define LOG_CONF_WITH_COLOR 3
/*---------------------------------------------------------------------------*/
static loramac_state_t state;

/*Retransmit and query timer*/
static struct ctimer retransmit_timer;
static struct ctimer query_timer;

static process_event_t loramac_state_change_event;
static process_event_t loramac_event_output;

/*Counters*/
static uint8_t next_seq = 0;
static uint8_t expected_seq = 0;
static uint8_t retransmit_attempt=0;

static uint16_t last_sent_datalen;
static lora_frame_hdr_t last_sent_frame;
//static uint8_t last_sent_data[LORA_PAYLOAD_BYTE_MAX_SIZE];//TODO usefull ??

PROCESS(loramac_process, "LoRa-MAC process");

/*---------------------------------------------------------------------------*/
void
change_notify_state(loramac_state_t new_state)//done
{
    state = new_state;
    process_post(&loramac_process, loramac_state_change_event, NULL);
}
/*---------------------------------------------------------------------------*/
void
loramac_send(void)//done
{
    process_post(&loramac_process, loramac_event_output, NULL);
}
/*---------------------------------------------------------------------------*/
void
on_query_timeout(void *ptr)
{

}
/*---------------------------------------------------------------------------*/
void
on_retransmit_timeout(void *ptr)
{

}
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
void
on_join_response(void)
{
    if( state == ALONE &&
        loraaddr_compare(lorabuf_get_addr(LORABUF_ADDR_RECEIVER), &lora_node_addr)==0 &&
        lorabuf_get_data_len()==1 &&
        lorabuf_get_attr(LORABUF_ATTR_MAC_SEQNO)==0)
    {
        retransmit_attempt = 0;

        lora_addr_t new_addr;
        lorabuf_copy_to(&new_addr.prefix);
        new_addr.id = node_id;
        loraaddr_set_node_addr(&new_addr);

        ctimer_stop(&retransmit_timer);
        ctimer_set(&query_timer, LORAMAC_QUERY_TIMEOUT, on_query_timeout, NULL);
        expected_seq ++;

        change_notify_state(READY);
    }else
    {
        printf("warn incorrect join_response\n");//todo
    }
}
/*---------------------------------------------------------------------------*/
void
on_data(void)
{
    uint8_t seq = lorabuf_get_attr(LORABUF_ATTR_MAC_SEQNO);
    if(seq < expected_seq){
        printf("sn smaller than expected");//todo
        return;
    }
    ctimer_stop(&retransmit_timer);
    ctimer_stop(&query_timer);
    retransmit_attempt = 0;
    if(seq > expected_seq){
        printf("lost x frames");//todo
    }
    expected_seq = seq+1;
    bridge_input();
    if(lorabuf_get_attr(LORABUF_ATTR_MAC_NEXT)){
        LORAPHY_SET_PARAM(LORAPHY_PARAM_WDT, LORAMAC_RETRANSMIT_TIMEOUT);
        LORAPHY_RX();
        ctimer_restart(&retransmit_timer);
    }else{
        ctimer_restart(&query_timer);
        change_notify_state(READY);
    }
}
/*---------------------------------------------------------------------------*/
void
on_ack(void)//done
{
    uint8_t seq = lorabuf_get_attr(LORABUF_ATTR_MAC_SEQNO);
    if(seq != last_sent_frame.seqno){
        LOG_WARN("Incorrect ACK SN:\n  expected:%d actual:%d", last_sent_frame.seqno, seq);
        return;
    }
    if (loraaddr_compare(&lora_root_addr, lorabuf_get_addr(LORABUF_ADDR_RECEIVER))!=0){
        LOG_WARN("ACK not for root\n");
        return;
    }
    ctimer_stop(&retransmit_timer);
    retransmit_attempt = 0;
    if(last_sent_frame.command == QUERY){
        ctimer_restart(&query_timer);
    }
    change_notify_state(READY);
}
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
int
loramac_input(void)//done
{
    lora_addr_t *dest_addr = lorabuf_get_addr(LORABUF_ADDR_RECEIVER);
    if(! loraaddr_is_in_dag(dest_addr)){
        return 0;
    }
    loramac_command_t command = lorabuf_get_attr(LORABUF_ATTR_MAC_CMD);
    switch (command) {
        case JOIN_RESPONSE:
            if(state == ALONE){
                on_join_response();
            }
            break;
        case DATA:
            if(state != ALONE){
                on_data();
            }
            break;
        case ACK:
            if(state != ALONE){
                on_ack();
            }
            break;
        default:
            printf("unknown MAC command\n");
    }
    return 0;
}
/*---------------------------------------------------------------------------*/
void
send_join_request(void)//done
{
    lorabuf_set_addr(LORABUF_ADDR_SENDER, &lora_node_addr);
    lorabuf_set_addr(LORABUF_ADDR_RECEIVER, &lora_root_addr);
    lorabuf_set_data_len(0);
    lorabuf_set_attr(LORABUF_ATTR_MAC_CMD, JOIN);
    lorabuf_set_attr(LORABUF_ATTR_MAC_SEQNO, next_seq);
    next_seq++;
    loramac_send();
}
/*---------------------------------------------------------------------------*/
void
loramac_root_start(void)//done
{
    state = ALONE;
    loramac_event_output = process_alloc_event();
    loramac_state_change_event = process_alloc_event();


    lora_addr_t lora_init_node_addr = {node_id, node_id};
    loraaddr_set_node_addr(&lora_init_node_addr);
    loraphy_init();
    send_join_request();

}
/*---------------------------------------------------------------------------*/
void
loramac_send_one(void)//done
{
    /*set packet seqno*/
    lorabuf_set_attr(LORABUF_ATTR_MAC_SEQNO, next_seq);
    next_seq++;

    /*copy param and addresses from buffer to last_send_frame*/
    memcpy(&last_sent_frame, lorabuf_mac_param_ptr(), sizeof(lora_frame_hdr_t)-(2* sizeof(lora_addr_t)));
    memcpy(&last_sent_frame.src_addr, lorabuf_get_addr(LORABUF_ADDR_FIRST), 2*sizeof(lora_addr_t));

    /*create str packet un lorabuf_c*/
    create(lorabuf_c_get_buf());

    /*send packet to PHY layer*/
    LORAPHY_SET_PARAM(LORAPHY_PARAM_WDT, 0);
    LORAPHY_TX(lorabuf_c_get_buf());

    /*actions depending on if a response is expected or not */
    if(last_sent_frame.confirmed || last_sent_frame.command == QUERY || last_sent_frame.command == JOIN){
        ctimer_set(&retransmit_timer, LORAMAC_RETRANSMIT_TIMEOUT, on_retransmit_timeout, NULL);
        LORAPHY_SET_PARAM(LORAPHY_PARAM_WDT, LORAMAC_RETRANSMIT_TIMEOUT);
        LORAPHY_RX();
    }else{
        change_notify_state(READY);
    }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(loramac_process, ev, data)//done
{
    /*
     * - wait loramac_event_output if no output packet pending
     * - state -> wait_response
     * - loramac_send_one()
     * - wait event loramac_state_change_event
     */
    PROCESS_BEGIN();
    bool pending = false;

    while(true){
        if(!pending){
            PROCESS_WAIT_EVENT_UNTIL(ev == loramac_event_output);
        }else{
            pending = false;
        }
        state = WAIT_RESPONSE;
        loramac_send_one();

        while(state != READY){
            PROCESS_WAIT_EVENT();
            if(ev == loramac_state_change_event){
                continue;
            }
            if(ev == loramac_event_output){
                pending = true;
            }
        }
    }
    PROCESS_END();
}
