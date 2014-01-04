/** 
 * -----------------------------------------------------------------------------
 * Clovece nezlob se (Server) - simple board game
 * 
 * Server for board game Clovece nezlob se using UDP datagrams for communication
 * with clients and SEND-AND-WAIT method to ensure that all packets arrive
 * and that they arrive in correct order. 
 * 
 * Semestral work for "Uvod do pocitacovich siti" KIV/UPS at
 * University of West Bohemia.
 * 
 * -----------------------------------------------------------------------------
 * 
 * File: client.c
 * Description: Handles all operations with connected clients
 * 
 * -----------------------------------------------------------------------------
 * 
 * @author: Martin Kucera, 2014
 * @version: 1.02
 * 
 */

#ifndef CLIENT_H
#define	CLIENT_H

#define RECONNECT_CODE_LEN 4

#include <sys/time.h>

#include "queue.h"
#include "global.h"

/* Reconnect codes */
extern char *reconnect_code[MAX_CONCURRENT_CLIENTS];

/* Global client number */
extern unsigned int client_num;

typedef struct {
    /* Client access mutex */
    pthread_mutex_t mtx_client;
    
    /* Client state - 1 active, 0 inactive*/
    unsigned short state;
    /* Client address */
    struct sockaddr_in *addr;
    /* Client address as a string */
    char *addr_str;
    
    /* Client index in an array */
    int client_index;
    
    /* Sequantial ID of sent packets to client */
    int pkt_send_seq_id;
    /* Sequential ID of received packets from client */
    int pkt_recv_seq_id;
    
    /* Timestamp of last communication with client */
    struct timeval timestamp;
    
    /* Output datagram queue */
    Queue *dgram_queue;
    
    /* Current game index */
    unsigned int game_index;
    
    /* Reconnect code */
    char *reconnect_code;
    
} client_t;

/* Function prototypes */
void add_client(struct sockaddr_in *addr);
void reconnect_client(client_t *client, struct sockaddr_in *addr);
client_t* get_client_by_addr(struct sockaddr_in *addr);
client_t* get_client_by_index(int index);
void release_client(client_t *client);
void remove_client(client_t **client);
void update_client_timestamp(client_t *client);
void clear_all_clients();
int get_client_index_by_rcode(char *code);
int generate_reconnect_code(char *s, int iteration);
void send_reconnect_code(client_t *client);

#endif	/* CLIENT_H */

