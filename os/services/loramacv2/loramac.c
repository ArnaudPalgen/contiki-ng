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
static char* mac_states_str[3] = {"ALONE", "READY", "WAIT_RESPONSE"};
static char* mac_command_str[5] = {"JOIN", "JOIN_RESPONSE", "DATA", "ACK", "QUERY"};
/*---------------------------------------------------------------------------*/
static loramac_state_t state;

/*Retransmit and query timer*/
static struct ctimer retransmit_timer;
static struct ctimer query_timer;

static process_event_t loramac_state_change_event;
static process_event_t loramac_event_output;
static process_event_t loramac_phy_done;
static process_event_t loramac_event_has_next;
process_event_t loramac_network_joined;

/*Counters*/
static uint8_t next_seq = 0;
static uint8_t expected_seq = 0;
static uint8_t retransmit_attempt=0;

static lora_frame_hdr_t last_sent_frame;
static uint16_t last_sent_datalen;
static uint8_t last_sent_data[LORA_PAYLOAD_BYTE_MAX_SIZE];

static bool pending = false;
static bool is_retransmission = false;

PROCESS(loramac_process, "LoRa-MAC process");

/*---------------------------------------------------------------------------*/
void
loramac_print_hdr(lora_frame_hdr_t *hdr)
{
    printf("src: ");
    loraaddr_print(&(hdr->src_addr));
    printf("|dest: ");
    loraaddr_print(&(hdr->dest_addr));
    printf("|K: %s|next: %s", hdr->confirmed ? "true":"false", hdr->next ? "true":"false");
    printf("|cmd: %s", mac_command_str[hdr->command]);
    printf("|seq: %d", hdr->seqno);
}
/*---------------------------------------------------------------------------*/
void
change_notify_state(loramac_state_t new_state)
{
    LOG_DBG("STATE %s -> %s | NOTIFY\n", mac_states_str[state], mac_states_str[new_state]);
    state = new_state;
    process_post(&loramac_process, loramac_state_change_event, NULL);
}
/*---------------------------------------------------------------------------*/
void
loramac_send(void)
{
    LOG_INFO("READY: %s\n", (state == READY) ? "true" : "false");
    if(state == READY) {
        lorabuf_set_attr(LORABUF_ATTR_MAC_CMD, DATA);
        lorabuf_set_addr(LORABUF_ADDR_RECEIVER, &lora_root_addr);
        pending = true;
        //process_post_synch(&loramac_process, loramac_event_output, (process_data_t) false);//todo review this
        process_post(&loramac_process, loramac_event_output, (process_data_t) false);
    }
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
    pending = true;
    process_post(&loramac_process, loramac_event_output, (process_data_t) false);
    LOG_DBG("RESTART query timer\n");
    ctimer_restart(&query_timer);
}
/*---------------------------------------------------------------------------*/
void
on_retransmit_timeout(void *ptr)
{
    LOG_DBG("TIMEOUT retransmit\n");
    LOG_DBG("STOP retransmit timer\n");
    ctimer_stop(&retransmit_timer);
    if(retransmit_attempt < LORAMAC_MAX_RETRANSMIT){
        is_retransmission = true;
        LOG_DBG("copy last sent frame to lorabuf\n");

        //copy last sent frame data to the buffer
        lorabuf_clear();
        memcpy(lorabuf_mac_param_ptr(), &last_sent_frame, sizeof(lora_frame_hdr_t)-(2* sizeof(lora_addr_t)));
        memcpy(lorabuf_get_addr(LORABUF_ADDR_FIRST), &last_sent_frame.src_addr, 2*sizeof(lora_addr_t));
        memcpy(lorabuf_get_buf(), &last_sent_data, last_sent_datalen);
        lorabuf_set_data_len(last_sent_datalen);

        retransmit_attempt ++;

        LOG_DBG("attempts: %d\n", retransmit_attempt);
        LOG_DBG("RESTART retransmit timer\n");
        pending = true;
        process_post(&loramac_process, loramac_event_output, (process_data_t) true);
        ctimer_restart(&retransmit_timer);//todo review done in  the process ?
    }else{
        retransmit_attempt = 0;
        LOG_WARN("sending failed\n");
        if(last_sent_frame.command == JOIN){
            LOG_DBG("for join -> sleep Lora radio during %s\n",LORAMAC_JOIN_SLEEP_TIME_c);
            
            //(LORAMAC_JOIN_SLEEP_TIME_c + random_rand())%(180*CLOCK_SECOND)//todo
            LORAPHY_SLEEP(LORAMAC_JOIN_SLEEP_TIME_c);
            
            //todo  this value must be equal to the radio sleep time
            ctimer_restart(&retransmit_timer);//todo timer set done in the process ?
        }else{
            change_notify_state(READY);
        }
    }
}
/*---------------------------------------------------------------------------*/
void
on_join_response(void)
{
    LOG_DBG("ON JOIN_RESPONSE\n");
    LOG_DBG("   > datalen: %d (expected 1)\n", lorabuf_get_data_len());
    LOG_DBG("   > seqno: %d (expected 0)\n", lorabuf_get_attr(LORABUF_ATTR_MAC_SEQNO));
    if( state == ALONE &&
        loraaddr_compare(lorabuf_get_addr(LORABUF_ADDR_RECEIVER), &lora_node_addr) &&
        lorabuf_get_data_len()==1 &&
        lorabuf_get_attr(LORABUF_ATTR_MAC_SEQNO)==0)
    {
        LOG_DBG("STOP retransmit timer\n");
        retransmit_attempt = 0;
        ctimer_stop(&retransmit_timer);

        lora_addr_t new_addr;
        new_addr.id = node_id;
        memcpy(&(new_addr.prefix), lorabuf_get_buf(), 2);
        LOG_DBG("New ADDR: ");
        LOG_DBG_LORA_ADDR(&new_addr);
        loraaddr_set_node_addr(&new_addr);

        LOG_DBG("START query timer\n");
        ctimer_set(&query_timer, LORAMAC_QUERY_TIMEOUT, on_query_timeout, NULL);
        expected_seq ++;

        //process_post(PROCESS_BROADCAST, loramac_network_joined, NULL);//signal to all process that the LoRaMAC network is joined
        change_notify_state(READY);
        lora_network_joined();
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
        LOG_INFO("seqno of data packet smaller than expected");
        return;
    }
    ctimer_stop(&retransmit_timer);
    ctimer_stop(&query_timer);
    retransmit_attempt = 0;
    if(seq > expected_seq){
        LOG_INFO("%d frames lost\n", seq-expected_seq);
    }
    expected_seq = seq+1;
    LOG_DBG("call to bridge ended\n");
    if(lorabuf_get_attr(LORABUF_ATTR_MAC_NEXT)){
        LOG_INFO("HAS NEXT\n");
        process_post(&loramac_process, loramac_event_has_next, NULL);
    }else{
        LOG_DBG("RESTART query timer\n");
        ctimer_restart(&query_timer);
        change_notify_state(READY);
    }
    bridge_input();
}
/*---------------------------------------------------------------------------*/
void
on_ack(void)
{
    LOG_DBG("ON ACK\n");
    uint8_t seq = lorabuf_get_attr(LORABUF_ATTR_MAC_SEQNO);
    LOG_DBG("   > seqno: %d (expected %d)\n", seq, last_sent_frame.seqno);
    if(seq != last_sent_frame.seqno){
        LOG_WARN("Incorrect ACK SN:\n  expected:%d actual:%d", last_sent_frame.seqno, seq);
        return;
    }
    if (!loraaddr_compare(&lora_node_addr, lorabuf_get_addr(LORABUF_ADDR_RECEIVER))){
        LOG_WARN("ACK not for me\n");
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
void
loramac_input(void)
{
    LOG_DBG("LORAMAC INPUT\n");
    //print_lorabuf();
    lora_addr_t *dest_addr = lorabuf_get_addr(LORABUF_ADDR_RECEIVER);
    LOG_DBG("   > dest addr: ");
    LOG_DBG_LORA_ADDR(dest_addr);
    if(! loraaddr_is_in_dag(dest_addr)){
        LOG_INFO("input frame is not for this DAG\n");
        return;
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
}
/*---------------------------------------------------------------------------*/
void
send_join_request(void)
{
    /*
     *The joining sequence is described by de diagrame below
     *
     *    RPL_ROOT                                                                 LORA_ROOT
     *        | -----------------JOIN[(prefix=node_id[0:8], node_id)]----------------> |
     *        | <-- JOIN_RESPONSE[(prefix=node_id[0:8], node_id), data=new_prefix] --> |
    **/
    LOG_DBG("send join request\n");
    lorabuf_set_addr(LORABUF_ADDR_SENDER, &lora_node_addr);
    lorabuf_set_addr(LORABUF_ADDR_RECEIVER, &lora_root_addr);
    lorabuf_set_data_len(0);
    lorabuf_set_attr(LORABUF_ATTR_MAC_CMD, JOIN);
    pending = true;
    process_post(&loramac_process, loramac_event_output, (process_data_t) false);//todo false usefull ?
}
/*---------------------------------------------------------------------------*/
void
set_conf()
{
    static uint8_t i = 0;
    loraphy_param_t radio_config[LORAPHY_NUM_RADIO_PARAM] = {LORAPHY_PARAM_BW, LORAPHY_PARAM_CR, LORAPHY_PARAM_FREQ,
                                                 LORAPHY_PARAM_MODE, LORAPHY_PARAM_PWR, LORAPHY_PARAM_SF};
    char* initial_radio_config[LORAPHY_NUM_RADIO_PARAM] = {LORA_RADIO_BW, LORA_RADIO_CR, LORA_RADIO_FREQ,
                                                           LORA_RADIO_MODE, LORA_RADIO_PWR, LORA_RADIO_SF};
    if(i<LORAPHY_NUM_RADIO_PARAM) {
        LORAPHY_SET_PARAM(radio_config[i], initial_radio_config[i]);
        i++;
    }else{
        if (i==LORAPHY_NUM_RADIO_PARAM){//all radio parameters are set
            i++;
            /*Start the LoRaMAC process and send the JOIN request*/
            LOG_DBG("START LORAMAC PROCESS AND SEND JOIN REQUEST\n");
            process_start(&loramac_process, NULL);
            send_join_request();
        }else{//the sending of the JOIN request is done 
            process_post(&loramac_process, loramac_phy_done, NULL);
        }
    }
}
/*---------------------------------------------------------------------------*/
void
phy_callback(loraphy_sent_status_t status)
{
    switch (status) {
        case LORAPHY_SENT_DONE:
            LOG_DBG("status:LORAPHY_SENT_DONE\n");
            set_conf();
            break;
        case LORAPHY_INPUT_DATA:
            LOG_DBG("status:LORAPHY_INPUT_DATA\n");
            parse(lorabuf_c_get_buf(), lorabuf_get_data_c_len(), 10);//10 is the size of 'radio rx '
            loramac_input();
            process_post(&loramac_process, loramac_phy_done, NULL);
            break;
        default:
            LOG_WARN("unknown loraphy status\n");
    }
}
/*---------------------------------------------------------------------------*/
void
loramac_root_start(void)
{
    LOG_DBG("start root\n");
    state = ALONE; // initial state

    /* Create events for LoRaMAC */
    loramac_state_change_event = process_alloc_event();
    loramac_event_output = process_alloc_event();
    loramac_phy_done = process_alloc_event();
    loramac_event_has_next = process_alloc_event();
    loramac_network_joined = process_alloc_event();

    /* Set initial node addr */
    lora_addr_t lora_init_node_addr = {node_id, node_id};
    LOG_DBG("Initial node addr: ");
    LOG_DBG_LORA_ADDR(&lora_init_node_addr);
    loraaddr_set_node_addr(&lora_init_node_addr);

    /* Init PHY layer */
    loraphy_set_callback(&phy_callback);
    loraphy_init();

}
/*---------------------------------------------------------------------------*/
void
prepare_last_sent_frame()
{
    LOG_DBG("IS RETRANSMISSION: %s\n", is_retransmission ? "true":"false");
    if(!is_retransmission) {
        /* SET SEQNO */
        lorabuf_set_attr(LORABUF_ATTR_MAC_SEQNO, next_seq);
        next_seq++;

        /*copy packet from buffer to last_send_frame*/
        memcpy(&last_sent_frame, lorabuf_mac_param_ptr(), sizeof(lora_frame_hdr_t) - (2 * sizeof(lora_addr_t)));
        memcpy(&last_sent_frame.src_addr, lorabuf_get_addr(LORABUF_ADDR_FIRST), 2 * sizeof(lora_addr_t));
        last_sent_datalen = lorabuf_get_data_len();
        memcpy(&last_sent_data, lorabuf_get_buf(), last_sent_datalen);
    }

    /* change state if needed */
    if(last_sent_frame.command != JOIN){
        state = WAIT_RESPONSE;
    }

    /* create str packet to lorabuf_c */
    lorabuf_c_clear();
    int size = create(lorabuf_c_get_buf());
    lorabuf_set_data_c_len(size);
    is_retransmission = false;
}
/*---------------------------------------------------------------------------*/
#define WAIT_PHY() \
{ \
    ev = PROCESS_EVENT_NONE; \
    while(ev != loramac_phy_done){ \
        PROCESS_WAIT_EVENT(); \
    } \
}
#define PHY_ACTION(...) \
{ \
    __VA_ARGS__ \
    WAIT_PHY(); \
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(loramac_process, ev, data)
{
    //static char datdata[10]={"48656c6C6F"};
    PROCESS_BEGIN();
    LOG_DBG("BEGIN LORAMAC process\n");
    PHY_ACTION(LORAPHY_SET_PARAM(LORAPHY_PARAM_WDT, LORAMAC_RETRANSMIT_TIMEOUT_c);)

    while(true){
        LOG_WARN("h1");
        //LOG_INFO("A\n");
        //lorabuf_c_copy_from((const char*)datdata, 10);
        //PHY_ACTION(LORAPHY_TX(lorabuf_c_get_buf(), lorabuf_get_data_c_len());)
        //LOG_INFO("B \n");
        //PHY_ACTION(LORAPHY_RX();)
        if(!pending){
            LOG_WARN("h2");
            LOG_DBG("WAITING ...\n");
            PROCESS_YIELD_UNTIL(ev == loramac_event_has_next || ev == loramac_event_output);
            LOG_WARN("h3");
            //PROCESS_YIELD();
            LOG_INFO("YOYO\n");
            //PROCESS_WAIT_EVENT();
            //while(ev != loramac_event_has_next && ev!=loramac_event_output){
            //    PROCESS_WAIT_EVENT();
            //}
            if(ev == loramac_event_has_next){
                LOG_WARN("h4");
                LOG_DBG("Frame has next\n");
                //PHY_ACTION(LORAPHY_SET_PARAM(LORAPHY_PARAM_WDT, LORAMAC_RETRANSMIT_TIMEOUT_c);)
                PHY_ACTION(LORAPHY_RX();)
                LOG_DBG("PHY RX sended \n");
                LOG_DBG("SET RETRANSMIT TIMER\n");
                ctimer_set(&retransmit_timer, LORAMAC_RETRANSMIT_TIMEOUT, on_retransmit_timeout, NULL);
                continue;
            }else if(ev == loramac_event_output){
                LOG_WARN("h5");
                LOG_DBG("receive packet to send\n");
            }
        }else{
            LOG_WARN("h6");
            LOG_DBG("PENDING packet\n");
        }
        LOG_WARN("h7");
        pending = false;
        /*------------------------------------------------------------------*/
        //LOG_DBG("send wdt 0\n");
        //PHY_ACTION(LORAPHY_SET_PARAM(LORAPHY_PARAM_WDT, LORAMAC_DISABLE_WDT);)
        /*------------------------------------------------------------------*/
        /*prepare the packet for transmission*/
        LOG_DBG("prepare packet\n");
        prepare_last_sent_frame();
        /*------------------------------------------------------------------*/
        /*send packet to PHY layer*/
        LOG_DBG("send TX\n");
        PHY_ACTION(LORAPHY_TX(lorabuf_c_get_buf(), lorabuf_get_data_c_len());)
        /*------------------------------------------------------------------*/
        /*actions depending on if a response is expected or not */
        if(last_sent_frame.confirmed || last_sent_frame.command == QUERY || last_sent_frame.command == JOIN){
            LOG_DBG("frame need a response\n");
            //PHY_ACTION(LORAPHY_SET_PARAM(LORAPHY_PARAM_WDT, LORAMAC_RETRANSMIT_TIMEOUT_c);)
            LORAPHY_RX();
            LOG_DBG("SET RETRANSMIT TIMER\n");
            ctimer_set(&retransmit_timer, LORAMAC_RETRANSMIT_TIMEOUT, on_retransmit_timeout, NULL);
            WAIT_PHY();
        }else{
            LOG_DBG("Frame don't need a response\n");
            LOG_DBG("STATE %s -> %s\n", mac_states_str[state], mac_states_str[READY]);
            state = READY;
        }
    }
    PROCESS_END();
}
