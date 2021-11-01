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
static char* mac_states_str[3] = {"ALONE", "READY", "WAIT_RESPONSE"};
static char* mac_command_str[5] = {"JOIN", "JOIN_RESPONSE", "DATA", "ACK", "QUERY"};
/*---------------------------------------------------------------------------*/
static loramac_state_t state;

/*Retransmit and query timer*/
static struct ctimer retransmit_timer;
static struct ctimer query_timer;

static process_event_t loramac_state_change_event;
static process_event_t loramac_event_output;
static process_event_t loramac_event_continue;
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

PROCESS(loramac_phy_waiter, "LoRa-MAC PHY waiter");
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
    LOG_DBG("<change_notify_state> STATE %s -> %s | NOTIFY\n", mac_states_str[state], mac_states_str[new_state]);
    state = new_state;
    process_post(&loramac_process, loramac_state_change_event, NULL);
}
/*---------------------------------------------------------------------------*/
void
loramac_send(void)
{
    //if(state == READY){
    //    LOG_DBG("<loramac_send> post loramac_event_output to loramac_process\n");
    //    process_post(&loramac_process, loramac_event_output, (process_data_t) false);
    //}
    //if(state == READY)
    lorabuf_set_attr(LORABUF_ATTR_MAC_CMD, DATA);
    lorabuf_set_addr(LORABUF_ADDR_RECEIVER, &lora_root_addr);
    pending = true;
    process_post(&loramac_process, loramac_event_output, (process_data_t) false);
}
/*---------------------------------------------------------------------------*/
//review test it
void
on_query_timeout(void *ptr)
{
    LOG_DBG("<on_query_timeout> TIMEOUT query\n");
    LOG_DBG("   - STOP query timer\n");
    ctimer_stop(&query_timer);
    lorabuf_set_addr(LORABUF_ADDR_SENDER, &lora_node_addr);
    lorabuf_set_addr(LORABUF_ADDR_RECEIVER, &lora_root_addr);
    lorabuf_set_data_len(0);
    lorabuf_set_attr(LORABUF_ATTR_MAC_CMD, QUERY);
    //lorabuf_set_attr(LORABUF_ATTR_MAC_SEQNO, next_seq);
    //next_seq++;
    //loramac_send();
    pending = true;
    process_post(&loramac_process, loramac_event_output, (process_data_t) false);
    LOG_DBG("   - RESTART query timer\n");
    ctimer_restart(&query_timer);
}
/*---------------------------------------------------------------------------*/
void
on_retransmit_timeout(void *ptr)
{
    LOG_DBG("<on_retransmit_timeout> TIMEOUT retransmit\n");
    LOG_DBG("   - STOP retransmit timer\n");
    ctimer_stop(&retransmit_timer);
    if(retransmit_attempt < LORAMAC_MAX_RETRANSMIT){
        LOG_DBG("   - copy last sent frame to lorabuf\n");

        //copy last sent frame data to the buffer
        lorabuf_clear();
        memcpy(lorabuf_mac_param_ptr(), &last_sent_frame, sizeof(lora_frame_hdr_t)-(2* sizeof(lora_addr_t)));
        memcpy(lorabuf_get_addr(LORABUF_ADDR_FIRST), &last_sent_frame.src_addr, 2*sizeof(lora_addr_t));
        memcpy(lorabuf_get_buf(), &last_sent_data, last_sent_datalen);
        lorabuf_set_data_len(last_sent_datalen);

        retransmit_attempt ++;

        LOG_DBG("   - attempts: %d\n", retransmit_attempt);
        LOG_DBG("   - RESTART retransmit timer\n");
        pending = true;
        process_post(&loramac_process, loramac_event_output, (process_data_t) true);
        ctimer_restart(&retransmit_timer);
    }else{
        retransmit_attempt = 0;
        LOG_WARN("sending failed\n");
        if(last_sent_frame.command == JOIN){
            LOG_DBG("   - for join -> sleep Lora radio during %s\n",LORAMAC_JOIN_SLEEP_TIME_c);
            LORAPHY_SLEEP(LORAMAC_JOIN_SLEEP_TIME_c);
            ctimer_restart(&retransmit_timer);
        }else{
            change_notify_state(READY);
        }
    }
}
/*---------------------------------------------------------------------------*/
void
on_join_response(void)
{
    LOG_DBG("<on_join_response>\n");
    LOG_DBG("   > datalen: %d (expected 1)\n", lorabuf_get_data_len());
    LOG_DBG("   > seqno: %d (expected 0)\n", lorabuf_get_attr(LORABUF_ATTR_MAC_SEQNO));
    if( state == ALONE &&
        loraaddr_compare(lorabuf_get_addr(LORABUF_ADDR_RECEIVER), &lora_node_addr) &&
        lorabuf_get_data_len()==1 &&
        lorabuf_get_attr(LORABUF_ATTR_MAC_SEQNO)==0)
    {
        LOG_DBG("   - STOP retransmit timer\n");
        retransmit_attempt = 0;
        ctimer_stop(&retransmit_timer);

        lora_addr_t new_addr;
        new_addr.id = node_id;
        memcpy(&(new_addr.prefix), lorabuf_get_buf(), 2);
        LOG_DBG("   - New ADDR: ");
        LOG_DBG_LORA_ADDR(&new_addr);
        loraaddr_set_node_addr(&new_addr);

        LOG_DBG("   - START query timer\n");
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
//review test it
void
on_data(void)
{
    LOG_DBG("<on_data>\n");
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
    bridge_input();
    LOG_DBG("call to bridge ended\n");
    if(lorabuf_get_attr(LORABUF_ATTR_MAC_NEXT)){
        //LOG_DBG("   - has next: true\n");
        //LOG_DBG("   - listen\n");
        ////fixme the two following line can't be used in a function -> move to process
        //LORAPHY_SET_PARAM(LORAPHY_PARAM_WDT, LORAMAC_RETRANSMIT_TIMEOUT_c);
        //LORAPHY_RX();
        //LOG_DBG("   - RESTART retransmit timer\n");
        //ctimer_restart(&retransmit_timer);
        process_post(&loramac_process, loramac_event_has_next, NULL);
    }else{
        LOG_DBG("   - RESTART query timer\n");
        ctimer_restart(&query_timer);
        change_notify_state(READY);
    }
}
/*---------------------------------------------------------------------------*/
void
on_ack(void)
{
    LOG_DBG("<on_ack>\n");
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
    LOG_DBG("   - STOP retransmit timer\n");
    ctimer_stop(&retransmit_timer);
    retransmit_attempt = 0;
    if(last_sent_frame.command == QUERY){
        LOG_DBG("   - ACK is the response of a query\n");
        LOG_DBG("   - RESTART query timer\n");
        ctimer_restart(&query_timer);
    }
    change_notify_state(READY);
}
/*---------------------------------------------------------------------------*/
void
loramac_input(void)
{
    LOG_DBG("<loramac_input>\n");
    print_lorabuf();
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
    LOG_DBG("<send_join_request>\n");
    lorabuf_set_addr(LORABUF_ADDR_SENDER, &lora_node_addr);
    lorabuf_set_addr(LORABUF_ADDR_RECEIVER, &lora_root_addr);
    lorabuf_set_data_len(0);
    lorabuf_set_attr(LORABUF_ATTR_MAC_CMD, JOIN);
    pending = true;
    process_post(&loramac_process, loramac_event_output, (process_data_t) false);
}
/*---------------------------------------------------------------------------*/
void
set_conf()
{
    LOG_DBG("<set_conf>\n");
    static uint8_t i = 0;
    loraphy_param_t radio_config[LORAPHY_NUM_RADIO_PARAM] = {LORAPHY_PARAM_BW, LORAPHY_PARAM_CR, LORAPHY_PARAM_FREQ,
                                                 LORAPHY_PARAM_MODE, LORAPHY_PARAM_PWR, LORAPHY_PARAM_SF};
    char* initial_radio_config[LORAPHY_NUM_RADIO_PARAM] = {LORA_RADIO_BW, LORA_RADIO_CR, LORA_RADIO_FREQ,
                                                           LORA_RADIO_MODE, LORA_RADIO_PWR, LORA_RADIO_SF};
    if(i<LORAPHY_NUM_RADIO_PARAM) {
        LOG_DBG("SET RADIO CONF %d\n", i);
        LORAPHY_SET_PARAM(radio_config[i], initial_radio_config[i]);
        i++;
    }else{
        LOG_DBG("conf done\n");
        if (i==LORAPHY_NUM_RADIO_PARAM){
            i++;
            /*Start the LoRaMAC process and send the JOIN request*/
            LOG_DBG("START LORAMAC PROCESs AND SEND JOIND REQUEST\n");
            process_start(&loramac_phy_waiter, NULL);
            process_start(&loramac_process, NULL);
            send_join_request();
        }else{
            LOG_DBG("POST TO WAITER\n");
            process_post(&loramac_phy_waiter, loramac_phy_done, NULL);
        }
    }
}
/*---------------------------------------------------------------------------*/
void
phy_callback(loraphy_sent_status_t status)
{
    LOG_DBG("PHY CALLBACK\n");
    switch (status) {
        case LORAPHY_SENT_DONE:
            LOG_DBG("status:LORAPHY_SENT_DONE\n");
            set_conf();
            //if(state == ALONE) {
            //    set_conf();
            //}else if (process_is_running(&loramac_process)){
            //    process_post(&loramac_phy_waiter, loramac_phy_done, NULL);
            //}
            break;
        case LORAPHY_INPUT_DATA:
            LOG_DBG("status:LORAPHY_INPUT_DATA\n");
            process_post(&loramac_phy_waiter, loramac_phy_done, NULL);
            parse(lorabuf_c_get_buf(), lorabuf_get_data_c_len());
            loramac_input();
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
    loramac_event_continue = process_alloc_event();
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

    ///*Start the LoRaMAC process and send the JOIN request*/
    //process_start(&loramac_process, NULL);
    //send_join_request();
}
/*---------------------------------------------------------------------------*/
void
prepare_last_sent_frame(bool is_retransmission)
{
    LOG_DBG("IS RETRANSMISSION: %s\n", is_retransmission ? "true":"false");
    if(!is_retransmission) {
        /* set SEQNO*/
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

    /*create str packet to lorabuf_c*/
    lorabuf_c_clear();
    int size = create(lorabuf_c_get_buf());
    lorabuf_set_data_c_len(size);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(loramac_phy_waiter, ev, data) {
    PROCESS_BEGIN();

    while (true) {
        PROCESS_WAIT_EVENT();
        while (ev != loramac_phy_done) {
            PROCESS_WAIT_EVENT();
        }
        LOG_DBG("POST CONTINUE TO LORAMAC PROCESS\n");
        process_post(&loramac_process, loramac_event_continue, NULL);
    }

    PROCESS_END();
}
/*---------------------------------------------------------------------------*/
#define WAIT_PHY PROCESS_WAIT_EVENT_UNTIL(ev == loramac_event_continue)
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(loramac_process, ev, data)
{
    PROCESS_BEGIN();
    LOG_DBG("BEGIN LORAMAC process\n");

    while(true){
        /*------------------------------------------------------------------*/
        if(!pending){
            LOG_DBG("WAIT a packet to send\n");
            PROCESS_WAIT_EVENT_UNTIL(ev == loramac_event_output);
        }else{
            LOG_DBG("PENDING packet\n");
        }
        pending = false;
        /*------------------------------------------------------------------*/
        LOG_DBG("BEGIN PHY TX\n");
        LOG_DBG("send wdt 0\n");
        LORAPHY_SET_PARAM(LORAPHY_PARAM_WDT, LORAMAC_DISABLE_WDT);
        WAIT_PHY;
        LOG_DBG("wdt done\n");
        /*------------------------------------------------------------------*/
        /*prepare the packet for transmission*/
        LOG_DBG("prepare packet\n");
        prepare_last_sent_frame((bool) data);
        LOG_DBG("packet prepared\n");
        /*------------------------------------------------------------------*/
        /*send packet to PHY layer*/
        LOG_DBG("send tx\n");
        LORAPHY_TX(lorabuf_c_get_buf(), lorabuf_get_data_c_len());
        WAIT_PHY;
        LOG_DBG("PHY TX DONE\n");
        /*------------------------------------------------------------------*/
        /*actions depending on if a response is expected or not */
        if(last_sent_frame.confirmed || last_sent_frame.command == QUERY || last_sent_frame.command == JOIN){
            LOG_DBG("Frame need a response\n");
            LORAPHY_SET_PARAM(LORAPHY_PARAM_WDT, LORAMAC_RETRANSMIT_TIMEOUT_c);
            WAIT_PHY;
            LORAPHY_RX();
            WAIT_PHY;
            LOG_DBG("PHY RX sended \n");
            LOG_DBG("SET retransmit timer\n");
            ctimer_set(&retransmit_timer, LORAMAC_RETRANSMIT_TIMEOUT, on_retransmit_timeout, NULL);

        }else{
            LOG_DBG("Frame don't need a response\n");
            LOG_DBG("STATE %s -> %s\n", mac_states_str[state], mac_states_str[READY]);
            state = READY;
        }
        /*------------------------------------------------------------------*/
        while(state != READY && !ctimer_expired(&retransmit_timer) && state !=ALONE){
            LOG_DBG(" wait state is READY\n");
            PROCESS_WAIT_EVENT();
            if(ev == loramac_event_has_next){
                LOG_DBG("Frame need a response\n");
                LORAPHY_SET_PARAM(LORAPHY_PARAM_WDT, LORAMAC_RETRANSMIT_TIMEOUT_c);
                WAIT_PHY;
                LORAPHY_RX();
                WAIT_PHY;
                LOG_DBG("PHY RX sended \n");
                LOG_DBG("SET retransmit timer\n");
                ctimer_set(&retransmit_timer, LORAMAC_RETRANSMIT_TIMEOUT, on_retransmit_timeout, NULL);
            }
        }
        LOG_DBG("state is ready\n");
        /*------------------------------------------------------------------*/
    }
    PROCESS_END();
}
