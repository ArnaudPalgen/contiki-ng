#include "loramac-conf.h"

/*---------------------------------------------------------------------------*/
/*macros definition*/

#define HEADER_SIZE 16 //Number of hexadecimal characters in the header
#define PAYLOAD_MAX_SIZE (2*UIP_CONF_BUFFER_SIZE)
#define FRAME_SIZE (HEADER_SIZE+PAYLOAD_MAX_SIZE)

#define UART_EXP_RESP_SIZE 2 // size of array that contain uart expected responses

#define K_FLAG 0x80
#define NEXT_FLAG 0x40


#define LOG_INFO_UFRAME(...)    LOG_UFRAME(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_DBG_UFRAME(...)    LOG_UFRAME(LOG_LEVEL_DBG, __VA_ARGS__)

#define LOG_UFRAME(level, uart_frame) do {  \
                           if(level <= (LOG_LEVEL)) { \
                                print_uart_frame(uart_frame); \
                           } \
                         } while (0)

/*---------------------------------------------------------------------------*/
/*enum definition*/
/*The possible UART reponse from the RN2483*/
typedef enum uart_response{
    UART_OK,
    UART_INVALID_PARAM,
    UART_RADIO_ERR,
    UART_RADIO_RX,
    UART_BUSY,
    UART_RADIO_TX_OK,
    UART_U_INT,
    UART_NONE
}uart_response_t;

/*The currently supported LoRaMAC commands*/
typedef enum mac_command {
    JOIN,
    JOIN_RESPONSE,
    DATA,
    ACK,
    QUERY,
}mac_command_t;

/*The type of the data field of an UART frame*/
typedef enum uart_frame_type{
    STR,
    LORA,
    INT,
}uart_type;

/*The UART commands for the RN2483*/
typedef enum uart_command{
    MAC_PAUSE,// pause mac layer
    SET_MOD,//set radio mode (fsk or lora)
    SET_FREQ,//set radio freq from 433050000 to 434790000 or from 863000000 to 870000000, in Hz.
    SET_WDT,//set watchdog timer
    RX,//receive mode
    TX,//transmit data
    SLEEP//system sleep
}uart_command_t;

/*---------------------------------------------------------------------------*/
/*structure definition*/

/*A LoRaMAC address*/
typedef struct lora_addr{
    uint8_t prefix;
    uint16_t id;
}lora_addr_t;

/*A LoRaMAC frame*/
typedef struct lora_frame{
    lora_addr_t src_addr; // The source Address
    lora_addr_t dest_addr; // The destination Address
    bool k; // true if the frame need an ack in return, false otherwise
    uint8_t seq; // The sequence number of the frame
    bool next; // true if another frame follow this frame. Only for downward traffic
    mac_command_t command; // The MAC command of the frame
    
    char* payload;// The payload. Must be a char array with hexadecimal characters
}lora_frame_t;

/*An UART frame*/
typedef struct uart_frame{
    uart_command_t cmd; // The UART command
    uart_type type; // The type of the data field

    union uart_data {
        char *s; // a string
        int d; // an integer
        lora_frame_t lora_frame; // a lora_frame
    } data; // The data that will be sent

    // an array containing the possible UART responses
    uart_response_t expected_response[UART_EXP_RESP_SIZE];
}uart_frame_t;

/*---------------------------------------------------------------------------*/
/*public functions*/

/* Init de PHY layer */
void phy_init();

/*Register a listener to call when a loraframe is available*/
void phy_register_listener(int (* listener)(lora_frame_t frame));

/* Send a lora_frame with the PHY layer */
int phy_tx(lora_frame_t frame);

/* Set the watchdog timer of the RN2483 */
int phy_timeout(int timeout);

/* Put de RN2483 in sleep mode (in ms) */
int phy_sleep(int duration);

/* Put de RN2483 in receive mode */
int phy_rx();
/*---------------------------------------------------------------------------*/
/*print functions*/
void print_uart_frame(uart_frame_t *frame);
void print_lora_frame(lora_frame_t *frame);
void print_lora_addr(lora_addr_t *addr);


/* logging macros */
#define LOG_INFO_LR_FRAME(...)    LOG_LR_FRAME(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_DBG_LR_FRAME(...)    LOG_LR_FRAME(LOG_LEVEL_DBG, __VA_ARGS__)

#define LOG_LR_FRAME(level, lora_frame) do {  \
                           if(level <= (LOG_LEVEL)) { \
                                print_lora_frame(lora_frame); \
                           } \
                         } while (0)

#define LOG_INFO_LR_ADDR(...)    LOG_LR_ADDR(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_DBG_LR_ADDR(...)    LOG_LR_ADDR(LOG_LEVEL_DBG, __VA_ARGS__)

#define LOG_LR_ADDR(level, lora_addr) do {  \
                           if(level <= (LOG_LEVEL)) { \
                                print_lora_addr(lora_addr); \
                           } \
                         } while (0)
