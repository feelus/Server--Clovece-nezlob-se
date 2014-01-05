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

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "client.h"
#include "client.h"
#include "queue.h"
#include "com.h"
#include "logger.h"
#include "game.h"

/* Array of connected clients */
client_t *clients[MAX_CONCURRENT_CLIENTS] = {NULL};
/* Number of clients connected  */
unsigned int client_num = 0;
/* Reconnect codes */
char *reconnect_code[MAX_CONCURRENT_CLIENTS] = {NULL};

/* Logger buffer */
char log_buffer[LOG_BUFFER_SIZE];

/**
 * void add_client(struct sockaddr_in *addr)
 * 
 * Takes input sockaddr_in and checks if it isn't already present in the
 * client connected array. If it isn't, creates new client_t structure and
 * associates it's members. Afterwards inserts newly created client into
 * the client array. Client's ID is permanent for the whole durration
 * of connection.
 */
void add_client(struct sockaddr_in *addr) {
    client_t *existing_client;
    client_t *new_client;
    struct sockaddr_in *new_addr;
    int i;
    
    if(client_num < MAX_CONCURRENT_CLIENTS) {
        existing_client = get_client_by_addr(addr);
        
        if(existing_client == NULL) {
            /* Allocate memory for new client */
            new_client = (client_t *) malloc(sizeof(client_t));

            /* Allocate memory for client address */
            new_addr = (struct sockaddr_in *) malloc(sizeof(struct sockaddr_in));

            /* Save copy of addr */
            memcpy(new_addr, addr, sizeof(struct sockaddr_in));

            /* Assign new client members */
            new_client->state = 1;
            new_client->pkt_recv_seq_id = 1;
            new_client->pkt_send_seq_id = 1;
            new_client->addr = new_addr;
            new_client->addr_str = malloc(INET_ADDRSTRLEN + 1);
            new_client->dgram_queue = malloc(sizeof(Queue));
            new_client->game_index = -1;
            new_client->reconnect_code = (char *) malloc(RECONNECT_CODE_LEN + 1);
            
            pthread_mutex_init(&new_client->mtx_client, NULL);

            queue_init(new_client->dgram_queue);
            inet_ntop(AF_INET, &addr->sin_addr, new_client->addr_str, INET_ADDRSTRLEN);

            /* Update timestamp */
            update_client_timestamp(new_client);

            /* Add client to array */
            for(i = 0; i < MAX_CONCURRENT_CLIENTS; i++) {
                if(clients[i] == NULL) {                
                    new_client->client_index = i;
                    clients[i] = new_client;

                    client_num++;
                    break;
                }
            }
            
            /* Assign reconnect code */
            generate_reconnect_code(new_client->reconnect_code, 0);
            reconnect_code[new_client->client_index] = new_client->reconnect_code;            

            sprintf(log_buffer,
                    "Added new client with IP address: %s and port %d",
                    new_client->addr_str,
                    htons(addr->sin_port)
                    );
            
            log_line(log_buffer, LOG_INFO);
            
            /* Stats */
            num_connections++;
        }
        else {
            release_client(existing_client);
        }
    }
    else {
        log_line("New client tried to connec but server is full", LOG_INFO);
        inform_server_full(addr);
    }
}

/**
 * void reconnect_client(client_t *client, struct sockaddr_in *addr)
 * 
 * Updates existing client's address and if he was in game, sends him game
 * state nad informs other players that he reconnected.
 */
void reconnect_client(client_t *client, struct sockaddr_in *addr) {
    char buff[100];
    int i;
    packet_t *pkt;
    game_t *game;
        
    if(client) {
        client->state = 1;
        client->pkt_recv_seq_id = 1;
        client->pkt_send_seq_id = 1;      

        /* Update timestamp */
        update_client_timestamp(client);

        /* Copy address */
        memcpy(client->addr, addr, sizeof(struct sockaddr_in));
        
        pkt = queue_front(client->dgram_queue);
        
        while(pkt) {
            queue_pop(client->dgram_queue, 0);
            
            /* Check if payload was built before freeing */
            if(pkt->state) {
                free(pkt->payload);
            }
            
            free(pkt->msg);
            free(pkt);
            
            pkt = queue_front(client->dgram_queue);
        }
        
        inet_ntop(AF_INET, &addr->sin_addr, client->addr_str, INET_ADDRSTRLEN);
        
        /* Send ACK */
        send_ack(client, 1, 0);

        /* Client was in game */
        if(client->game_index != -1) {
            game = get_game_by_index(client->game_index);;
            
            if(game) {
                /* Find player id within game */
                for(i = 0; i < 4; i++) {
                    if(game->player_index[i] == client->client_index) {
                        break;
                    }
                }

                /* Notify players */
                sprintf(buff,
                        "CLIENT_RECONNECT;%d",
                        i
                        );

                broadcast_game(game, buff, client, 0);

                /* Send game state to client */
                send_game_state(client, game);
                
                /* Release game */
                release_game(game);
            }
        }

        /* Log */
        sprintf(log_buffer,
                "Reconnected client IP address: %s and port %d",
                client->addr_str,
                htons(addr->sin_port)
                );

        log_line(log_buffer, LOG_INFO);
    }
}

/* 
 * client_t* get_client(struct sockaddr_in *addr)
 * 
 * Loops through the connected client array, trying to find
 * a 
 * Searches through connected clients to find matching
 * address and port. If it finds a match, returns client
 * with it's mutex locked, has to be released afterwards
 * with release_client(client_t *client)
 */
client_t* get_client_by_addr(struct sockaddr_in *addr) {
    int i = 0;
    char addr_str[INET_ADDRSTRLEN];
    
    inet_ntop(AF_INET, &(addr)->sin_addr, addr_str, INET_ADDRSTRLEN);
    
    for(i = 0; i < MAX_CONCURRENT_CLIENTS; i++) {
        if(clients[i] != NULL) {
            pthread_mutex_lock(&clients[i]->mtx_client);

            /* Check if client still exists */
            if(clients[i] != NULL) {

                    /* Check if address and port matches */
                    if(strncmp(clients[i]->addr_str, addr_str, INET_ADDRSTRLEN) == 0
                            && (htons((addr)->sin_port) == htons(clients[i]->addr->sin_port) )) {
                        
                        return clients[i];

                    }
            }

            release_client(clients[i]);
        }
    }
    
    return NULL;
}

/* 
 * client_t* get_client_by_index(int index)
 * 
 * Returns client at given index and locks him.
 * If no client is at that index, returns NULL. 
 */
client_t* get_client_by_index(int index) {
    if(index>= 0 && MAX_CONCURRENT_CLIENTS > index && clients[index]) {    
        pthread_mutex_lock(&clients[index]->mtx_client);
        
        return clients[index];
    }
    
    return NULL;
}

/*
 * void release_client(client_t *client)
 * 
 * Tries to release client's mutex
 */
void release_client(client_t *client) {    
    if(client && pthread_mutex_trylock(&client->mtx_client) != 0) {
        pthread_mutex_unlock(&client->mtx_client);
    }
    else {
        log_line("Tried to release non-locked client", LOG_WARN);
    }
}

/*
 * void remove_client(client_t **client)
 * 
 * Removes client from client array, before deleting, wakes
 * up a possible thread waiting for its mutex.
 * 
 */
void remove_client(client_t **client) {            
    if(client != NULL) {
        sprintf(log_buffer,
                "Removing client with IP address: %s and port %d",
                (*client)->addr_str,
                htons((*client)->addr->sin_port)
                );
        
        log_line(log_buffer, LOG_INFO);
        
        clients[(*client)->client_index] = NULL;
        reconnect_code[(*client)->client_index] = NULL;
        
        free((*client)->addr);
        free((*client)->addr_str);
        free((*client)->reconnect_code);
        
        client_num --;
        
        /* Release client */
        release_client((*client));
        
        free((*client)->dgram_queue);
        free((*client));
    }
    
    *client = NULL;
}

/**
 * void update_client_timestamp(client_t *client)
 * 
 * Updates client's timestamp to current time
 */
void update_client_timestamp(client_t *client) {
    if(client != NULL) {
        gettimeofday(&client->timestamp, NULL);
    }
}

/**
 * void clear_all_clients()
 * 
 * Removes (and frees) all clients
 */
void clear_all_clients() {
    int i = 0;
    client_t *client;
    
    for(i = 0; i < MAX_CONCURRENT_CLIENTS; i++) {
        client = get_client_by_index(i);
        
        if(client) {
            clients[i] = NULL;
            
            free(client->addr);
            free(client->addr_str);
            free(client->dgram_queue);
            free(client->reconnect_code);
                        
            pthread_mutex_unlock(&client->mtx_client);
            free(client);
        }
    }
}

/**
 * int get_client_index_by_rcode(char *code)
 * 
 * Searches through reconnect codes looking for a match. If match was found,
 * array index is the client index.
 */
int get_client_index_by_rcode(char *code) {
    int i;
    
    for(i = 0; i < MAX_CONCURRENT_CLIENTS; i++) {
        if(reconnect_code[i] && strncmp(reconnect_code[i], code, RECONNECT_CODE_LEN) == 0) {
            return i;
        }
    }
    
    return -1;
}

/**
 * int generate_reconnect_code(char *s, int iteration)
 * 
 * Generates unique code for client to use for reconnecting.
 */
int generate_reconnect_code(char *s, int iteration)  {
    int existing_index;
    
    if(iteration > 100) {
        s[0] = 0;
        
        return 0;
    }
    
    gen_random(s, RECONNECT_CODE_LEN);
    existing_index = get_client_index_by_rcode(s);
    
    if(existing_index != -1) {
        generate_reconnect_code(s, iteration++);
    }
    
    return 1;
}

/**
 * void send_reconnect_code(client_t *client)
 * 
 * Sends generated reconnection code to client
 */
void send_reconnect_code(client_t *client) {
    char *buff = (char *) malloc(30 + strlen(client->reconnect_code));
    
    sprintf(buff,
            "RECONNECT_CODE;%s",
            client->reconnect_code
            );
    
    enqueue_dgram(client, buff, 1);
    
    free(buff);
}
