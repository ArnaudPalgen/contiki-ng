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
#define LOG_LEVEL LOG_LEVEL_DBG
#define LOG_CONF_WITH_COLOR 3
static char* mac_states_str[3] = {"ALONE", "READY", "WAIT_RESPONSE"};
/*---------------------------------------------------------------------------*/
static loramac_state_t state;

/*Retransmit and query timer*/
static struct ctimer retransmit_timer;
static struct ctimer query_timer;

static process_event_t loramac_state_change_event;
static process_event_t loramac_event_output;
process_event_t loramac_network_joined;

/*Counters*/
static uint8_t next_seq = 0;
static uint8_t expected_seq = 0;
static uint8_t retransmit_attempt=0;

//static uint16_t last_sent_datalen;
static lora_frame_hdr_t last_sent_frame;
//static uint8_t last_sent_data[LORA_PAYLOAD_BYTE_MAX_SIZE];//TODO usefull ??

PROCESS(loramac_process, "LoRa-MAC process");

/*---------------------------------------------------------------------------*/
void
change_notify_state(loramac_state_t new_state)//done
{
    LOG_DBG("STATE %s -> %s\n", mac_states_str[state], mac_states_str[new_state]);
    LOG_DBG("notify new state\n");
    state = new_state;
    process_post(&loramac_process, loramac_state_change_event, NULL);
}
/*---------------------------------------------------------------------------*/
void
loramac_send(void)//done
{
    LOG_DBG("post loramac_event_output to loramac_process\n");
    process_post(&loramac_process, loramac_event_output, NULL);
}
/*---------------------------------------------------------------------------*/
void
on_query_timeout(void *ptr)
{
    LOG_DBG("TIMEOUT query\n");
    LOG_DBG("STOP query timer\n");
    ctimer_stop(&query_timer);
    lorabuf_set_addr(LORABUF_ADDR_SENDER, &lora_node_addr);
    lorabuf_set_addr(LORABUF_ADDR_RECEIVER, &lora_root_addr);
    lorabuf_set_data_len(0);
    lorabuf_set_attr(LORABUF_ATTR_MAC_CMD, QUERY);
    lorabuf_set_attr(LORABUF_ATTR_MAC_SEQNO, next_seq);
    next_seq++;
    loramac_send();
}
/*---------------------------------------------------------------------------*/
void
on_retransmit_timeout(void *ptr)//done
{
    LOG_DBG("TIMEOUT retransmit\n");
    LOG_DBG("STOP retransmit timer\n");
    ctimer_stop(&retransmit_timer);
    if(retransmit_attempt < LORAMAC_MAX_RETRANSMIT){
        LOG_DBG("retransmit\n");
        loramac_send();
        retransmit_attempt ++;
        LOG_DBG("attempts: %d\n", retransmit_attempt);
        LORAPHY_SET_PARAM(LORAPHY_PARAM_WDT, LORAMAC_RETRANSMIT_TIMEOUT_c);
        LORAPHY_RX();
        LOG_DBG("RESTART retransmit timer\n");
        ctimer_restart(&retransmit_timer);
    }else{
        retransmit_attempt = 0;
        LOG_WARN("sending failed\n");
        if(last_sent_frame.command == JOIN){
            LOG_DBG("for join -> sleep Lora radio during %s\n",LORAMAC_JOIN_SLEEP_TIME_c);
            LORAPHY_SLEEP(LORAMAC_JOIN_SLEEP_TIME_c);
            ctimer_restart(&retransmit_timer);
        }else{
            change_notify_state(READY);
        }
    }
}
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
void
on_join_response(void)
{
    LOG_DBG("ON JOIN_RESPONSE\n");
    LOG_DBG("   > datalen: %d\n (expected 1)", lorabuf_get_data_len());
    LOG_DBG("   > seqno: %d\n (expected 0)", lorabuf_get_attr(LORABUF_ATTR_MAC_SEQNO));
    if( state == ALONE &&
        loraaddr_compare(lorabuf_get_addr(LORABUF_ADDR_RECEIVER), &lora_node_addr)==0 &&
        lorabuf_get_data_len()==1 &&
        lorabuf_get_attr(LORABUF_ATTR_MAC_SEQNO)==0)
    {
        LOG_DBG("STOP retransmit timer\n");
        retransmit_attempt = 0;
        ctimer_stop(&retransmit_timer);

        lora_addr_t new_addr;
        //lorabuf_copy_to(&new_addr.prefix);
        loraaddr_copy(&new_addr, &lora_node_addr);
        new_addr.id = node_id;
        loraaddr_set_node_addr(&new_addr);

        LOG_DBG("START query timer\n");
        ctimer_set(&query_timer, LORAMAC_QUERY_TIMEOUT, on_query_timeout, NULL);
        expected_seq ++;

        process_post(PROCESS_BROADCAST, loramac_network_joined, NULL);//signal to all process that the LoRaMAC network is joined
        change_notify_state(READY);
    }else
    {
        LOG_WARN("incorrect join_response\n");
    }
}
/*---------------------------------------------------------------------------*/
void
on_data(void)
{
    LOG_DBG("ON DATA\n");
    uint8_t seq = lorabuf_get_attr(LORABUF_ATTR_MAC_SEQNO);
    LOG_DBG("   > seqno: %d (expected %d)\n", seq, expected_seq);
    if(seq < expected_seq){
        LOG_INFO("seqno smaller than expected");
        return;
    }
    ctimer_stop(&retransmit_timer);
    ctimer_stop(&query_timer);
    retransmit_attempt = 0;
    if(seq > expected_seq){
        LOG_INFO("%d frames lost\n", seq-expected_seq);
    }
    expected_seq = seq+1;
    bridge_input();
    if(lorabuf_get_attr(LORABUF_ATTR_MAC_NEXT)){
        LOG_DBG("has next: true\n");
        LOG_DBG("listen\n");
        LORAPHY_SET_PARAM(LORAPHY_PARAM_WDT, LORAMAC_RETRANSMIT_TIMEOUT_c);
        LORAPHY_RX();
        LOG_DBG("RESTART retransmit timer\n");
        ctimer_restart(&retransmit_timer);
    }else{
        LOG_DBG("RESTART query timer\n");
        ctimer_restart(&query_timer);
        change_notify_state(READY);
    }
}
/*---------------------------------------------------------------------------*/
void
on_ack(void)//done
{
    LOG_DBG("ON ACK\n");
    uint8_t seq = lorabuf_get_attr(LORABUF_ATTR_MAC_SEQNO);
    LOG_DBG("   > seqno: %d (expected %d)\n", seq, last_sent_frame.seqno);
    if(seq != last_sent_frame.seqno){
        LOG_WARN("Incorrect ACK SN:\n  expected:%d actual:%d", last_sent_frame.seqno, seq);
        return;
    }
    if (loraaddr_compare(&lora_root_addr, lorabuf_get_addr(LORABUF_ADDR_RECEIVER))!=0){
        LOG_WARN("ACK not for root\n");
        return;
    }
    LOG_DBG("STOP retransmit timer\n");
    ctimer_stop(&retransmit_timer);
    retransmit_attempt = 0;
    if(last_sent_frame.command == QUERY){
        LOG_DBG("ACK is the response of a query\n");
        LOG_DBG("RESTART query timer\n");
        ctimer_restart(&query_timer);
    }
    change_notify_state(READY);
}
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
int
loramac_input(void)//done
{
    LOG_DBG("LORAMAC INPUT\n");
    lora_addr_t *dest_addr = lorabuf_get_addr(LORABUF_ADDR_RECEIVER);
    LOG_DBG("   > dest addr:");
    LOG_DBG_LR_ADDR(dest_addr);
    if(! loraaddr_is_in_dag(dest_addr)){
        LOG_INFO("Trame is not for this DAG\n");
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
            LOG_WARN("unknown MAC command\n");
    }
    return 0;
}
/*---------------------------------------------------------------------------*/
void
send_join_request(void)//done
{
    LOG_DBG("send join request\n");
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
    LOG_DBG("start root\n");
    state = ALONE;
    loramac_event_output = process_alloc_event();
    loramac_state_change_event = process_alloc_event();
    loramac_network_joined = process_alloc_event();

    lora_addr_t lora_init_node_addr = {node_id, node_id};
    loraaddr_set_node_addr(&lora_init_node_addr);
    LOG_DBG("CALL PHY INIT\n");
    loraphy_init();
    LOG_DBG("PHY INIT DONE\n");
    process_start(&loramac_process, NULL);
    send_join_request();

}
/*---------------------------------------------------------------------------*/
void
loramac_send_one(void)//done
{
    LOG_DBG("SEND ONE\n");
    /*set packet seqno*/
    lorabuf_set_attr(LORABUF_ATTR_MAC_SEQNO, next_seq);
    next_seq++;

    /*copy param and addresses from buffer to last_send_frame*/
    memcpy(&last_sent_frame, lorabuf_mac_param_ptr(), sizeof(lora_frame_hdr_t)-(2* sizeof(lora_addr_t)));
    memcpy(&last_sent_frame.src_addr, lorabuf_get_addr(LORABUF_ADDR_FIRST), 2*sizeof(lora_addr_t));

    /*send packet to PHY layer*/
    LORAPHY_SET_PARAM(LORAPHY_PARAM_WDT, LORAMAC_DISABLE_WDT);

    /*create str packet un lorabuf_c*/
    create(lorabuf_c_get_buf());
    LORAPHY_TX(lorabuf_c_get_buf());

    /*actions depending on if a response is expected or not */
    if(last_sent_frame.confirmed || last_sent_frame.command == QUERY || last_sent_frame.command == JOIN){
        ctimer_set(&retransmit_timer, LORAMAC_RETRANSMIT_TIMEOUT, on_retransmit_timeout, NULL);
        LORAPHY_SET_PARAM(LORAPHY_PARAM_WDT, LORAMAC_RETRANSMIT_TIMEOUT_c);
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
    LOG_DBG("BEGIN LORAMAC process\n");
    bool static pending = false;

    while(true){
        if(!pending){
            LOG_DBG("WAIT a packet to send\n");
            PROCESS_WAIT_EVENT_UNTIL(ev == loramac_event_output);
        }else{
            LOG_DBG("packet to send pending\n");
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
