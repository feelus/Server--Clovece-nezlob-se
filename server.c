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
 * File: server.c
 * Description: Handles packet processing
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

#include "err.h"
#include "server.h"
#include "global.h"
#include "client.h"
#include "com.h"
#include "game.h"
#include "logger.h"

/* Server started */
struct timeval ts_start;

/* Logger buffer */
char log_buffer[LOG_BUFFER_SIZE];

/* Server socket */
int server_sockfd;

/*
 * void init_server(char *bind_ip, int port)
 * 
 * Starts the server and its threads,
 * binds to given ip and port.
 */
void init_server(char *bind_ip, int port) {
    int server_len;
    struct sockaddr_in server_addr;
    
    server_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(bind_ip);
    server_addr.sin_port = htons(port);
    
    server_len = sizeof(server_addr);
    
    if(bind(server_sockfd, (struct sockaddr *) &server_addr, server_len) != 0) {
        raise_error("Error binding, exiting.");
    }
    
    /* Set socket timeout depending on OS */
    set_socket_timeout_linux();
    
    /* Log */
    sprintf(log_buffer,
            "Starting server with IP %s and port %d",
            bind_ip,
            port
            );
    
    log_line(log_buffer, LOG_ALWAYS);
}

/**
 * void process_dgram(char *dgram, struct sockaddr_in *addr)
 * 
 * Processes accepted datagram. Checks if sequential ID is correct, if it's lower,
 * we resend the ACK packet and dont bother with that datagram anymore, because
 * it was already processed before.
 */
void process_dgram(char *dgram, struct sockaddr_in *addr) {
    /* Protocol token */
    char *token;
    /* Token length */
    int token_len;
    /* Command */
    char *type;
    /* Generic char buffer */
    char *generic_chbuff;
    /* Sequential ID of received packet */
    int packet_seq_id;
    /* Client which we receive from */
    client_t *client;
    /* Generic unsigned int var */
    unsigned int generic_uint;
    /* String representation of address */
    char addr_str[INET_ADDRSTRLEN];

    inet_ntop(AF_INET, &addr->sin_addr, addr_str, INET_ADDRSTRLEN);
    
    /* Log */
    sprintf(log_buffer,
            "DATA_IN: %s <--- %s:%d",
            dgram,
	    addr_str,
	    htons(addr->sin_port)
            );
    log_line(log_buffer, LOG_DEBUG);
    
    token = strtok(dgram, ";");
    token_len = strlen(token);
    
    generic_chbuff = strtok(NULL, ";");
    packet_seq_id = (int) strtol(generic_chbuff, NULL, 0);
    
    /* Check if datagram belongs to us */
    if( (token_len == strlen(STRINGIFY(APP_TOKEN))) &&
            strncmp(token, STRINGIFY(APP_TOKEN), strlen(STRINGIFY(APP_TOKEN))) == 0 &&
            packet_seq_id > 0) {
        
        type = strtok(NULL, ";");
        
        /* New client connection */
        if(strncmp(type, "CONNECT", 7) == 0) {
            
            add_client(addr);            
            client = get_client_by_addr(addr);
            
            if(client) {
                send_ack(client, 1, 0);
                send_reconnect_code(client);
                
                /* Release client */
                release_client(client);
            }
            
        }
        /* Reconnect */
        else if(strncmp(type, "RECONNECT", 9) == 0) {            
            client = get_client_by_index(get_client_index_by_rcode(strtok(NULL, ";")));
            
            if(client) {
                /* Sends ACK aswell after resetting clients SEQ_ID */
                reconnect_client(client, addr);
                
                /* Release client */
                release_client(client);
            }
        }
        /* Client should already exist */
        else {
            client = get_client_by_addr(addr);
            
            if(client != NULL) {
                /* Check if expected seq ID matches */
                if(packet_seq_id == client->pkt_recv_seq_id) {
                    
                    /* Get command */
                    if(strncmp(type, "CREATE_GAME", 11) == 0) {
                                                
                        /* ACK client */
                        send_ack(client, packet_seq_id, 0);
                        
                        create_game(client);
                        
                    }
                    /* Receive ACK packet */
                    else if(strncmp(type, "ACK", 3) == 0) {
                        
                        recv_ack(client, 
                                (int) strtoul(strtok(NULL, ";"), NULL, 10));
                        
                        update_client_timestamp(client);
                        
                    }
                    /* Close client connection */
                    else if(strncmp(type, "CLOSE", 5) == 0) {
                        
                        /* ACK client */
                        send_ack(client, packet_seq_id, 0);
                        
                        leave_game(client);
                        remove_client(&client);
                        
                    }
                    /* Keepalive loop */
                    else if(strncmp(type, "KEEPALIVE", 9) == 0) {
                        
                        /* ACK client */
                        send_ack(client, packet_seq_id, 0);
                        
                    }
                    /* Join existing game */
                    else if(strncmp(type, "JOIN_GAME", 9) == 0) {
                        
                        /* ACK client */
                        send_ack(client, packet_seq_id, 0);
                        
                        join_game(client, strtok(NULL, ";"));
                        
                    }
                    /* Leave existing game */
                    else if(strncmp(type, "LEAVE_GAME", 10) == 0) {
                        
                        /* ACK client */
                        send_ack(client, packet_seq_id, 0);
                        
                        leave_game(client);
                    }
                    /* Start game */
                    else if(strncmp(type, "START_GAME", 10) == 0) {
                        
                        /* ACK client */
                        send_ack(client, packet_seq_id, 0);
                        
                        start_game(client);
                    }
                    /* Rolling die */
                    else if(strncmp(type, "DIE_ROLL", 8) == 0) {
                        
                        /* ACK client */
                        send_ack(client, packet_seq_id, 0);
                        
                        roll_die(client);
                    }
                    /* Moving figure */
                    else if(strncmp(type, "FIGURE_MOVE", 11) == 0) {
                        
                        /* ACK client */
                        send_ack(client, packet_seq_id, 0);
                        
                        /* Parse figure id */
                        generic_chbuff = strtok(NULL, ";");
                        generic_uint = (unsigned int) strtoul(generic_chbuff, NULL, 10);

                        move_figure(client, generic_uint);
                    }

		   else if(strncmp(type, "MESSAGE", 7) == 0) {
			
			/* ACK client */
			send_ack(client, packet_seq_id, 0);
			
			broadcast_message(client, strtok(NULL, ";"));
		   }
                                        
                }
                /* Packet was already processed */
                else if(packet_seq_id < client->pkt_recv_seq_id &&
                        strncmp(type, "ACK", 3) != 0) {
                    
                    send_ack(client, packet_seq_id, 1);
                    
                }
                
                /* If client didnt close conection */
                if(client != NULL) {
                    
                    /* Release client */
                    release_client(client);
                }
            }
        }
    }
}

/**
 * void set_socket_timeout_linux()
 * 
 * Sets socket timeout on receiving calls to one second.
 */
void set_socket_timeout_linux() {
    struct timeval cur_tv;
    
    cur_tv.tv_sec = 1;
    cur_tv.tv_usec = 0;
    
    if(setsockopt(server_sockfd, SOL_SOCKET, SO_RCVTIMEO, &cur_tv, sizeof(cur_tv)) < 0) {
        raise_error("Error setting socket timeout (linux).");
    }
}
