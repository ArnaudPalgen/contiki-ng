#include "contiki.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "sys/node-id.h"
#include "loramac.h"
#include "net/linkaddr.h"
#include "sys/log.h"
#include "sys/mutex.h"
//#include "rn2483radio.h"

/*---------------------------------------------------------------------------*/
/* Log configuration */

#define LOG_MODULE "LoRa MAC"
#define LOG_LEVEL LOG_LEVEL_DBG
#define LOG_CONF_WITH_COLOR 3

//LoRa addr
static const lora_addr_t root_addr={ROOT_PREFIX, ROOT_ID};
lora_addr_t loramac_addr;

//MAC state
static state_t state;

static void (* upper_layer)(lora_addr_t *src, lora_addr_t *dest, char* data) = NULL;

//timers
static struct ctimer retransmit_timer;
static struct ctimer query_timer;

//buffers
static lora_frame_t buffer[BUF_SIZE];
static uint8_t w_i = 0;// index to write in the buffer
static uint8_t r_i = 0;// index to read in the buffer 
static uint8_t buf_len = 0;// current size of the buffer
mutex_t tx_buf_mutex;// mutex for tx_buffer

static lora_frame_t last_send_frame;
static uint8_t expected_seq = 0;
static uint8_t next_seq = 0;
static uint8_t retransmit_attempt=0;

static process_event_t new_tx_frame_event;//event that signals to the TX process that a new frame is available to be sent 
static process_event_t state_change_event;//event that signals to the TX process that ..TODO
process_event_t loramac_network_joined;

PROCESS(mac_tx, "LoRa-MAC tx process");

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
 * disable the watchdog timer and send the
 * loraframe to the PHY layer
 */
void
send_to_phy(lora_frame_t frame)
{
    phy_timeout(0);//disable watchdog timer
    phy_tx(frame);
}

/**
 * enqueue a loraframe in the tx_buffer if it's not full
 * and signal to process that a packet has been enqueued
 * 
 * Return: 1 if the buffer is full, 0 otherwise
 * 
 */
int
enqueue_packet(lora_frame_t frame)
{
    LOG_DBG("Enter enqueue_packet with frame: ");
    LOG_DBG_LR_FRAME(&frame);
    LOG_INFO("YOOOOOO 1\n");
    
    //acquire mutex for buffer
    while(!mutex_try_lock(&tx_buf_mutex)){}
    LOG_INFO("YOOOOOO 2\n");

    if(buf_len <= BUF_SIZE){
        LOG_DBG("append to buffer\n");
        buffer[w_i] = frame;
        buf_len++;
        w_i = (w_i+1)%BUF_SIZE;
        LOG_INFO("YOOOOOO 3\n");
        mutex_unlock(&tx_buf_mutex);
        LOG_DBG("post new_tx_frame_event to TX_PROCESS\n");
        
        // signal to process that a packet has been enqueued
        LOG_INFO("YOOOOOO 4\n");
        process_post(&mac_tx, new_tx_frame_event, NULL);
        return 0;
    }else{
        LOG_INFO("TX buffer full\n");
        mutex_unlock(&tx_buf_mutex);
        LOG_INFO("YOOOOOO 5\n");
        return 1;
    }
}


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
    //send_to_phy(ack_frame);
    //last_send_frame = ack_frame;
    enqueue_packet(ack_frame);
}

/*---------------------------------------------------------------------------*/
/* Timeout callback functions */
void
retransmit_timeout(void *ptr)
{
    LOG_INFO("retransmit timeout !\n");
    LOG_ERR("STOP retransmit timer thanks to retransmit_timeout\n");
    ctimer_stop(&retransmit_timer);
    if(retransmit_attempt < MAX_RETRANSMIT){
        LOG_DBG("retransmit frame: ");
        LOG_DBG_LR_FRAME(&last_send_frame);
        send_to_phy(last_send_frame);
        retransmit_attempt ++;
        LOG_ERR("RESTART retransmit timer because a frame was retransmit\n");
        phy_timeout(RX_TIME);
        phy_rx();
        ctimer_restart(&retransmit_timer);
    }else{
        LOG_INFO("Unable to send frame ");
        LOG_INFO_LR_FRAME(&last_send_frame);
        retransmit_attempt = 0;
        if(last_send_frame.command==QUERY){
            LOG_DBG("START query_timer because last send frame is a QUERY\n");
            ctimer_restart(&query_timer);
        }
        setState(READY);
    }
}

/**
 * stop query timer and enqueue a query frame
 */
void
query_timeout(void *ptr)
{
    LOG_INFO("Query timeout !\n");
    LOG_DBG("STOP query timer thanks to query_timeout\n");
    ctimer_stop(&query_timer);
    lora_frame_t query_frame;
    query_frame.src_addr = loramac_addr;
    query_frame.dest_addr=root_addr;
    query_frame.k=false;
    query_frame.seq=next_seq;
    query_frame.next=false;
    query_frame.command=QUERY;
    query_frame.payload="";
    LOG_DBG("query frame built\n");
    enqueue_packet(query_frame);
}

/*---------------------------------------------------------------------------*/
/* Packet processing functions */
void
on_join_response(lora_frame_t* frame)
{
    LOG_DBG("JOIN RESPONSE\n");
    if(state == ALONE && forRoot(&(frame->dest_addr)) && strlen(frame->payload) == 2 && frame->seq == 0){
        loramac_addr.prefix = (uint8_t) strtol(frame->payload, NULL, 16);
        LOG_DBG("state = READY\n");
        setState(READY);
        LOG_DBG("STOP retransmit timer\n");
        ctimer_stop(&retransmit_timer);
        
        LOG_INFO("Lora Root joined\n");
        LOG_INFO("Node addr: ");
        LOG_INFO_LR_ADDR(&loramac_addr);
        printf("\n");

        
        LOG_DBG("START TX_PROCESS\n");
        process_start(&mac_tx, NULL);
        LOG_DBG("SET query timer\n");
        ctimer_set(&query_timer, QUERY_TIMEOUT, query_timeout, NULL);
        expected_seq ++;
        process_post(PROCESS_BROADCAST, loramac_network_joined, NULL);
    }else{
        LOG_WARN("Incorrect JOIN_RESPONSE\n");
    }
}

void
on_data(lora_frame_t* frame)
{
    LOG_INFO("ON_DATA!\n");

    LOG_DBG("STOP retransmit and query timeout\n");
    ctimer_stop(&retransmit_timer);
    ctimer_stop(&query_timer);

    if(frame->seq < expected_seq){
        //the root has not received the last ack -> retransmit
        LOG_WARN("sequence number smaller than expected\n");
        if(frame->k){
            send_ack(frame->src_addr, frame->seq);
        }
    }else{
        if(frame->seq > expected_seq){
            // lost (frame->seq - expected_seq) packets
            LOG_WARN("lost %d frames\n", (frame->seq - expected_seq));
        }
        expected_seq = frame->seq+1;
        if(frame->k){
            // incoming frame need an ack
            send_ack(frame->src_addr, frame->seq);
        }
        if(frame->next){
            LOG_DBG("More data will follow -> listen\n");
            
            // puts the radio in listening mode
            phy_timeout(RX_TIME);
            phy_rx();

            //restart retransmit timer because next is true. i.e. a packet is expected.
            //if a retransmit timeout occur, it's because the packet has not been received.
            //The ack retransmission asks to the root node to retransmit the packet.
            
            ctimer_restart(&retransmit_timer);
        }else{
            // No data follows i.e. response to the QUERY is complete
            // -> retart the query_timer
            // -> set state to READY beacause state was set to
            //    WAIT_RESPONSE when sending the request
            // 
            LOG_DBG("no data follows -> state=Ready and restart query_timer\n");
            ctimer_restart(&query_timer);
            setState(READY);
        }
        if (upper_layer != NULL){//current
            upper_layer(&(frame->src_addr), &(frame->dest_addr), frame->payload);
        }else{
            LOG_WARN("Please register an upper_layer\n");
        }
    }
}

int
mac_send_packet(lora_addr_t src_addr, bool need_ack, void* data)
{
    lora_frame_t frame = {src_addr, root_addr, need_ack, 0, false, DATA, data};
    return enqueue_packet(frame);
}

void
on_ack(lora_frame_t* frame)
{
    LOG_INFO("ACK!\n");
    if(frame->seq != last_send_frame.seq){
        // the node can only receive an ack for the last packet sent
        LOG_WARN("Incorrect ACK SN:\n  expected:%d actual:%d", last_send_frame.seq, frame->seq);
    }else{
        LOG_DBG("STOP retransmit timer thanks to correct ACK\n");
        ctimer_stop(&retransmit_timer);
        if(last_send_frame.command==QUERY){
            ctimer_restart(&query_timer);
        }
        setState(READY);
        //mac_send_packet(root_addr, true, "ABC");
    }
}

int
lora_rx(lora_frame_t frame)
{
    LOG_INFO("RX LoRa frame: ");
    LOG_INFO_LR_FRAME(&frame);
    
    if(!forDag(&(frame.dest_addr))){
        /** dest addr is not for the node or for his dodag
         *  -> drop frame
         */
        LOG_DBG("not for me -> drop frame\n");
        return 0;
    }

    mac_command_t command = frame.command;

    switch (command){
        case JOIN_RESPONSE:
            on_join_response(&frame);
            break;
        case DATA:
            if(state != ALONE){
                on_data(&frame);
            }
            break;
        case ACK:
            on_ack(&frame);
            break;
        default:
            LOG_WARN("Unknown MAC command %d\n", command);
    }
    return 0;
}
/*---------------------------------------------------------------------------*/
/* Driver functions */



void 
mac_init()
{
    LOG_INFO("Init LoRa MAC\n");
    //set custom link_addr 
    //unsigned char new_linkaddr[8] = {'_','u','m','o','n','s',linkaddr_node_addr.u8[LINKADDR_SIZE - 2],linkaddr_node_addr.u8[LINKADDR_SIZE - 1]};
    //linkaddr_t new_addr;
    //memcpy(new_addr.u8, new_linkaddr, 8*sizeof(unsigned char));
    //linkaddr_set_node_addr(&new_addr);

    //LOG_INFO("Node ID: %u\n", node_id);
    //LOG_INFO("New Link-layer address: ");
    //LOG_INFO_LLADDR(&linkaddr_node_addr);
    //LOG_INFO("\n");
    
}

void
mac_root_start()
{
    LOG_INFO("Start LoRaMAC RPL root\n");
    /* set initial LoRa address */
    loramac_addr.prefix = node_id;//most significant 8 bits of the node_id
    loramac_addr.id = node_id;

    /* set initial state */
    state = ALONE;
    LOG_DBG("initial state: %d\n", state);

    loramac_network_joined = process_alloc_event();
    new_tx_frame_event = process_alloc_event();
    state_change_event = process_alloc_event();

    /* start phy layer */
    phy_init();
    phy_register_listener(&lora_rx);

    lora_frame_t join_frame = {loramac_addr, root_addr, false, next_seq, false, JOIN, ""};
    last_send_frame = join_frame;
    next_seq++;
    send_to_phy(join_frame);
    phy_timeout(RX_TIME);
    phy_rx();
    LOG_DBG("SET retransmit timer\n");
    ctimer_set(&retransmit_timer, RETRANSMIT_TIMEOUT, retransmit_timeout, NULL);
    LOG_DBG("initialization complete\n");
}

void
loramac_set_input_callback(void (* listener)(lora_addr_t *src, lora_addr_t *dest, char* data))//current
{
    upper_layer = listener;
}
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

            last_send_frame = buffer[r_i];
            LOG_DBG("frame from buffer: ");
            LOG_DBG_LR_FRAME(&last_send_frame);

            buf_len = buf_len-1;
            LOG_DBG("buffer size: %d\n", buf_len);
            r_i = (r_i+1)%BUF_SIZE;
            buf_not_empty = (buf_len>0);
            
            mutex_unlock(&tx_buf_mutex);
            
            last_send_frame.seq = next_seq;
            next_seq++;
            send_to_phy(last_send_frame);
            LOG_DBG("frame sended to PHY layer\n");
            
            if(last_send_frame.k || last_send_frame.command == QUERY){
                //to need to use setState(state) because the process does not need to advertise itself
                state = WAIT_RESPONSE;
                
                LOG_DBG("START retransmit timer in mac_tx process\n");
                ctimer_restart(&retransmit_timer);
                LOG_DBG("Listen during %d s\n", RX_TIME/1000);
                phy_timeout(RX_TIME);
                phy_rx();
                
                while(state != READY){
                    LOG_DBG("wait state change to READY\n");
                    PROCESS_WAIT_EVENT_UNTIL(ev == state_change_event);
                }
                LOG_DBG("state is READY\n");

            }

            //update buf_not_empty value
            while(!mutex_try_lock(&tx_buf_mutex)){}
            buf_not_empty = (buf_len>0);
            LOG_WARN("buf not empty: %d\n", buf_not_empty);
            mutex_unlock(&tx_buf_mutex);

        } while (buf_not_empty);
        
    }
    
    PROCESS_END();
}