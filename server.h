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
 * @version: 1.0
 * 
 */

#ifndef SERVER_H
#define	SERVER_H

#include <netinet/in.h>

/* Global server socket */
extern int server_sockfd;

/* Function prototypes */
void init_server(char *bind_ip, int port);
void process_dgram(char *dgram, struct sockaddr_in *addr);
void set_socket_timeout_linux();
void set_socket_timeout_windows();

#endif	/* SERVER_H */

