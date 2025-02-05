#include "contiki.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "sys/node-id.h"
#include "loramac.h"
#include "net/linkaddr.h"
#include "sys/log.h"
#include "sys/mutex.h"
#include "random.h"

/*---------------------------------------------------------------------------*/

/* Log configuration */
#define LOG_MODULE "LoRa MAC"
#define LOG_LEVEL LOG_LEVEL_INFO
#define LOG_CONF_WITH_COLOR 3

/*LoRaMAC addresses*/
static const lora_addr_t root_addr={ROOT_PREFIX, ROOT_ID}; // The ROOT address
lora_addr_t loramac_addr; // The node address

/*The current LoRaMAC state */
static state_t state;

/*The callback function for the upper layer*/
static void (* upper_layer)(lora_addr_t *src, lora_addr_t *dest, char* data) = NULL;

/*Retransmit and query timer*/
static struct ctimer retransmit_timer;
static struct ctimer query_timer;

/*Buffer related variables*/
static lora_frame_t buffer[BUF_SIZE];
static uint8_t w_i = 0; // index to write in the buffer
static uint8_t r_i = 0; // index to read in the buffer 
static uint8_t buf_len = 0;// current size of the buffer
mutex_t tx_buf_mutex; // mutex for tx_buffer

/*The last sent frame*/
static lora_frame_t last_send_frame;

/*Counters*/
static uint8_t expected_seq = 0;
static uint8_t next_seq = 0;
static uint8_t retransmit_attempt=0;

/*Events*/
static process_event_t new_tx_frame_event;//event that signals to the TX process that a new frame is available to be sent 
static process_event_t state_change_event;//event that signals to the TX process that the state has changed
process_event_t loramac_network_joined;//event that signal that the node has joined the LoRaMAC network

PROCESS(mac_tx, "LoRa-MAC tx process");

/*---------------------------------------------------------------------------*/
/* Functions that check dest of a lora_addr */
bool forDag(lora_addr_t *dest_addr){
    // if frame is for this RPL root or for RPL child of this root
    return dest_addr->prefix == loramac_addr.prefix || dest_addr->id == loramac_addr.id;
}

bool forRoot(lora_addr_t *dest_addr){
    return dest_addr->prefix == loramac_addr.prefix && dest_addr->id == loramac_addr.id;
}

bool forChild(lora_addr_t *dest_addr){
    return dest_addr->prefix == loramac_addr.prefix && dest_addr->id != loramac_addr.id;
}
/*---------------------------------------------------------------------------*/

/**
 * Set the state and signal the change to the process
 */
void
setState(state_t new_state)
{
    state = new_state;
    process_post(&mac_tx, state_change_event, NULL);
}

/**
 * Disable the watchdog timer and send the
 * loraframe to the PHY layer
 */
void
send_to_phy(lora_frame_t frame)
{
    phy_timeout(0);//disable watchdog timer
    phy_tx(frame);
}

/*---------------------------------------------------------------------------*/
/**
 * Enqueue a loraframe in the tx_buffer if it's not full
 * and signal to process that a packet has been enqueued
 * 
 * Return: 1 if the buffer is full, 0 otherwise
 * 
 */
int
enqueue_packet(lora_frame_t frame)
{
    LOG_DBG("want to add frame to tx buffer: ");
    LOG_DBG_LR_FRAME(&frame);
    
    /*acquire mutex for buffer*/
    while(!mutex_try_lock(&tx_buf_mutex)){}

    if(buf_len <= BUF_SIZE){
        buffer[w_i] = frame;
        buf_len++;
        w_i = (w_i+1)%BUF_SIZE;
        LOG_DBG("frame added\n");
        LOG_DBG("TX BUF SIZE: %d\n", buf_len);
        mutex_unlock(&tx_buf_mutex);
        LOG_DBG("post new_tx_frame_event to tx_process\n");
        
        /*signal to process that a packet has been enqueued*/
        process_post(&mac_tx, new_tx_frame_event, NULL);
        return 0;
    }else{
        LOG_WARN("TX BUF FULL\n");
        LOG_WARN("TX BUF SIZE: %d\n", buf_len);
        mutex_unlock(&tx_buf_mutex);
        return 1;
    }
}
/*---------------------------------------------------------------------------*/
/*Send an ack to the ack_dest_addr with the sequence number ack_seq*/
void
send_ack(lora_addr_t ack_dest_addr, uint8_t ack_seq)
{
    static lora_frame_t ack_frame;
    
    ack_frame.src_addr = loramac_addr;
    ack_frame.seq = ack_seq;
    ack_frame.command=ACK;
    ack_frame.next=false;
    ack_frame.k=false;
    ack_frame.dest_addr = ack_dest_addr;

    last_send_frame = ack_frame;
    LOG_DBG("send ack %d to ", ack_frame.seq);
    LOG_DBG_LR_ADDR(&(ack_frame.dest_addr));
    LOG_DBG("\n");
    enqueue_packet(ack_frame);
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/* Timeout callback functions */

/*Retransmit the last sended frame*/
void
retransmit_timeout(void *ptr)
{
    LOG_INFO("Retransmit timeout\n");
    LOG_DBG("STOP retransmit timer\n");
    ctimer_stop(&retransmit_timer);
    if(retransmit_attempt < MAX_RETRANSMIT){
        LOG_INFO("retransmit frame: ");
        LOG_DBG_LR_FRAME(&last_send_frame);
        
        send_to_phy(last_send_frame);
        retransmit_attempt ++;
        LOG_DBG("retransmit attempt: %d\n", retransmit_attempt);
        
        LOG_DBG("RESTART retransmit timer\n");
        
        phy_timeout(RX_TIME);
        phy_rx();
        ctimer_restart(&retransmit_timer);
    }else{
        LOG_INFO("Unable to send frame ");
        LOG_INFO_LR_FRAME(&last_send_frame);
        LOG_INFO("retransmit attempt: %d\n", retransmit_attempt);
        
        retransmit_attempt = 0;
        if (last_send_frame.command == JOIN){
            /*Unable to JOIN a LoRaMAC network -> Increase the retransmit timer time with some jitter*/
            ctimer_set(&retransmit_timer, 
                (RETRANSMIT_TIMEOUT+(random_rand() % RETRANSMIT_TIMEOUT))%(60*CLOCK_SECOND),
                retransmit_timeout, NULL);
            /*Feature: Put the RN2483 and the zolertia platform in sleep mode and if possible*/
        }else{
            if(last_send_frame.command == QUERY){
                LOG_DBG("RESTART query_timer\n");
                ctimer_restart(&query_timer);
            }
            setState(READY);
        }
    }
}
/*---------------------------------------------------------------------------*/
/*Send a query to ask downward traffic*/
void
query_timeout(void *ptr)
{
    LOG_INFO("Query timeout\n");
    LOG_DBG("STOP query timer\n");
    ctimer_stop(&query_timer);
    lora_frame_t query_frame;
    query_frame.src_addr = loramac_addr;
    query_frame.dest_addr=root_addr;
    query_frame.k=false;
    query_frame.seq=next_seq;
    query_frame.next=false;
    query_frame.command=QUERY;
    query_frame.payload="";
    enqueue_packet(query_frame);
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/* Packet processing functions */

/*Process a JOIN_RESPONSE frame*/
void
on_join_response(lora_frame_t* frame)
{
    LOG_DBG("JOIN RESPONSE FRAME\n");

    /*chech that:
     *  - the state is the initial state (ALONE)
     *  - the frame is for this node
     *  - the payload lenght is 2 because the prefix size is one byte
     *  - the sequence number is 0 because it must be the first received frame
     */
    if(state == ALONE && forRoot(&(frame->dest_addr)) && strlen(frame->payload) == 2 && frame->seq == 0){
        retransmit_attempt = 0;
        LOG_DBG("retransmit attempt: %d\n", retransmit_attempt);
        loramac_addr.prefix = (uint8_t) strtol(frame->payload, NULL, 16);
        LOG_DBG("state = READY\n");
        setState(READY);
        LOG_DBG("STOP retransmit timer\n");
        ctimer_stop(&retransmit_timer);
        
        LOG_INFO("LoRa root joined\n");
        LOG_INFO("Node addr: ");
        LOG_INFO_LR_ADDR(&loramac_addr);
        LOG_INFO("\n");

        
        LOG_DBG("START tx_process\n");
        process_start(&mac_tx, NULL);
        LOG_DBG("SET query timer\n");
        ctimer_set(&query_timer, QUERY_TIMEOUT, query_timeout, NULL);
        expected_seq ++;
        process_post(PROCESS_BROADCAST, loramac_network_joined, NULL);//signal to all process that the LoRaMAC network is joined
    }else{
        LOG_WARN("Incorrect JOIN_RESPONSE\n");
    }
}

/*---------------------------------------------------------------------------*/
/*Process a DATA frame*/
void
on_data(lora_frame_t* frame)
{
    LOG_DBG("DATA FRAME\n");

    if(frame->seq < expected_seq){
        /*The node has not received the last ack -> retransmit*/
        LOG_WARN("sequence number smaller than expected\n");
    }else{
        LOG_DBG("STOP retransmit timer\n");
        LOG_DBG("STOP retransmit query timer\n");
        ctimer_stop(&retransmit_timer);
        ctimer_stop(&query_timer);
        
        retransmit_attempt = 0;
        LOG_DBG("retransmit attempt: %d\n", retransmit_attempt);
        
        if(frame->seq > expected_seq){
            // lost (frame->seq - expected_seq) packets
            LOG_WARN("lost %d frames\n", (frame->seq - expected_seq));
        }
        expected_seq = frame->seq+1;

        if (upper_layer != NULL){
            upper_layer(&(frame->src_addr), &(frame->dest_addr), frame->payload);
        }else{
            LOG_WARN("Please register an upper_layer\n");
        }

        if(frame->next){
            LOG_DBG("More data will follow -> listen\n");
            
            // puts the radio in listening mode
            phy_timeout(RX_TIME);
            phy_rx();

            //restart retransmit timer because next is true. i.e. a packet is expected.
            //if a retransmit timeout occur, it's because the packet has not been received.
            //The ack retransmission asks to the root node to retransmit the packet.
            LOG_DBG("RESTART retransmit timer\n");
            ctimer_restart(&retransmit_timer);
        }else{
            // No data follows i.e. response to the QUERY is complete
            // -> retart the query_timer
            // -> set state to READY because state was set to
            //    WAIT_RESPONSE when sending the request
            // 
            LOG_DBG("no data follows");
            LOG_DBG("state = READY\n");
            LOG_DBG("RESTART query timer\n");
            ctimer_restart(&query_timer);
            setState(READY);
        }

    }
}

/*---------------------------------------------------------------------------*/
/*Proces an ACK frame*/
void
on_ack(lora_frame_t* frame)
{
    LOG_DBG("ACK FRAME\n");
    if(frame->seq != last_send_frame.seq){
        // the node can only receive an ack for the last packet sent
        LOG_WARN("Incorrect ACK SN:\n  expected:%d actual:%d", last_send_frame.seq, frame->seq);
    }else if(!forRoot(&(frame->dest_addr))){
        LOG_WARN("ACK NOT FOR ROOT\n");
    }
    else{
        LOG_DBG("STOP retransmit timer\n");
        ctimer_stop(&retransmit_timer);
        retransmit_attempt = 0;
        LOG_DBG("retransmit attempt: %d\n", retransmit_attempt);

        if(last_send_frame.command==QUERY){
            LOG_DBG("RESTART query timer\n");
            ctimer_restart(&query_timer);
        }
        LOG_DBG("state = READY\n");
        setState(READY);
    }
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*Process a received LoRaMAC frame*/
int
lora_rx(lora_frame_t frame)
{
    LOG_INFO("MAC RX: ");
    LOG_INFO_LR_FRAME(&frame);
    
    if(!forDag(&(frame.dest_addr))){
        /** dest addr is not for the node or for his DODAG
         *  -> drop frame
         */
        LOG_DBG("not for me -> drop frame\n");
        return 0;
    }

    // call the correct function to process the frame
    mac_command_t command = frame.command;

    switch (command){
        case JOIN_RESPONSE:
            if(state == ALONE){
                on_join_response(&frame);
            }
            break;
        case DATA:
            if(state != ALONE){
                on_data(&frame);
            }
            break;
        case ACK:
            if(state != ALONE){
                on_ack(&frame);
            }
            break;
        default:
            LOG_WARN("Unknown MAC command %d\n", command);
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*LoRaMAC driver functions*/

void
mac_root_start()
{
    LOG_INFO("Start LoRaMAC RPL root\n");
    
    /* set initial LoRa address */
    loramac_addr.prefix = node_id;//most significant 8 bits of the node_id
    loramac_addr.id = node_id;

    /* set initial state */
    state = ALONE;
    LOG_DBG("state = ALONE\n");

    /* create events */
    loramac_network_joined = process_alloc_event();
    new_tx_frame_event = process_alloc_event();
    state_change_event = process_alloc_event();

    /* set up the PHY layer */
    phy_init();
    phy_register_listener(&lora_rx);


    /* send the JOIN frame */
    lora_frame_t join_frame = {loramac_addr, root_addr, false, next_seq, false, JOIN, ""};
    last_send_frame = join_frame;
    next_seq++;
    send_to_phy(join_frame);
    
    /* listen */
    phy_timeout(RX_TIME);
    phy_rx();
    
    /* set up the retransmit timer */
    LOG_DBG("SET retransmit timer\n");
    ctimer_set(&retransmit_timer, RETRANSMIT_TIMEOUT, retransmit_timeout, NULL);
    LOG_DBG("initialization completed\n");
}

/*---------------------------------------------------------------------------*/
/*set the upper layer callback function that will be used when data are available*/
void
loramac_set_input_callback(void (* listener)(lora_addr_t *src, lora_addr_t *dest, char* data))
{
    upper_layer = listener;
}

/*---------------------------------------------------------------------------*/
/*Use this function to send data with LoRaMAC*/
int
mac_send_packet(lora_addr_t src_addr, bool need_ack, void* data)
{
    lora_frame_t frame = {src_addr, root_addr, need_ack, 0, false, DATA, data};
    return enqueue_packet(frame);
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/* TX process */
PROCESS_THREAD(mac_tx, ev, data){
    
    bool buf_not_empty = true;
    
    PROCESS_BEGIN();
    LOG_DBG("MAC_TX process started\n");
    
    while(true){
        PROCESS_WAIT_EVENT_UNTIL(ev==new_tx_frame_event);
        do
        {   
            //acquire mutex for buffer
            while(!mutex_try_lock(&tx_buf_mutex)){}

            // get the next frame to send
            last_send_frame = buffer[r_i];
            buf_len = buf_len-1;
            r_i = (r_i+1)%BUF_SIZE;
            buf_not_empty = (buf_len>0);

            LOG_DBG("read frame from tx buffer.");
            LOG_DBG("TX BUF SIZE: %d\n", buf_len);
            
            mutex_unlock(&tx_buf_mutex);
            
            last_send_frame.seq = next_seq;
            next_seq++;
            
            LOG_INFO("MAC TX: ");
            LOG_INFO_LR_FRAME(&last_send_frame);

            send_to_phy(last_send_frame);
            
            if(last_send_frame.k || last_send_frame.command == QUERY){
                //no need to use setState(state) because the process does not need to advertise itself
                state = WAIT_RESPONSE;
                
                LOG_DBG("START retransmit timer\n");
                ctimer_set(&retransmit_timer, RETRANSMIT_TIMEOUT, retransmit_timeout, NULL);
                LOG_DBG("listen during %d s\n", RX_TIME/1000);
                phy_timeout(RX_TIME);
                phy_rx();
                
                // the frame need a response
                // wait the response. i.e th state change to READY
                LOG_DBG("wait for state to be READY\n");
                while(state != READY){
                    PROCESS_WAIT_EVENT_UNTIL(ev == state_change_event);
                }
                LOG_DBG("state is READY\n");
            }

            //update buf_not_empty value
            while(!mutex_try_lock(&tx_buf_mutex)){}
            buf_not_empty = (buf_len>0);
            mutex_unlock(&tx_buf_mutex);

        } while (buf_not_empty);
  
    }
    
    PROCESS_END();
}
