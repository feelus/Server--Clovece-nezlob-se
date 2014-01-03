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
 * File: main.c
 * Description: Starts the server and handles user input.
 * 
 * -----------------------------------------------------------------------------
 * 
 * @author: Martin Kucera, 2014
 * @version: 1.0
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <arpa/inet.h>

#include "server.h"
#include "err.h"
#include "receiver.h"
#include "sender.h"
#include "game.h"
#include "game_watchdog.h"
#include "com.h"
#include "logger.h"
#include "global.h"

/* Receiver thread */
pthread_t thr_receiver; 
/* Sender thread */
pthread_t thr_sender; 
/* Watchdog thread */
pthread_t thr_watchdog;

/* Receiver mutex (if unclocked, receiver thread stops) */
pthread_mutex_t mtx_thr_receiver; 
/* Sender mutex (if unclocked, sender thread stops) */
pthread_mutex_t mtx_thr_sender; 
/* Watchdog mutex (if unclocked, Watchdog thread stops) */
pthread_mutex_t mtx_thr_watchdog;

/* Logger buffer */
char log_buffer[LOG_BUFFER_SIZE];

/**
* void help()
*
* Prints brief help, basic program usage.
*/
void help() {
    printf("NAME:\n");
    printf("\t\t server_cns - Simple board game\n");
    
    printf("--------------------------------------------------\n");
    printf("USAGE:\n");
    printf("\t\t server_cns <ip> <port> [logfile] [log_severity] [verbose_severity]\n");
    
    printf("--------------------------------------------------\n");
    printf("EXAMPLE:\n");
    printf("\t\t server_cns 0.0.0.0 1337\n");
    printf("\t\t server_cns 0.0.0.0 1337 debug_log.log\n");
    printf("\t\t server_cns 0.0.0.0 1337 debug_log.log 4\n");
    printf("\t\t server_cns 0.0.0.0 1337 debug_log.log 4 3\n");
    
    printf("--------------------------------------------------\n");
    printf("ARGUMENT DESC:\n");
    printf("\t\t <ip> - Bind IP address or hostname\n");
    printf("\t\t <port> - Bind port number.\n");
    printf("\t\t [logfile] - Filename which is used for logging.\n");
    printf("\t\t [log_severity] - Log severity for log file (includes all lower levels).\n");
    printf("\t\t [verbose_severity] - Which logs will be shown in command line (includes all lower levels).\n");
    
    printf("--------------------------------------------------\n");
    printf("LOG LEVELS:\n");
    printf("\t\t 0 - Only necessary server messages will be shown.\n");
    printf("\t\t 1 - Error messages.\n");
    printf("\t\t 2 - Warn messages.\n");
    printf("\t\t 3 - Information messages.\n");
    printf("\t\t 4 - Debugging messages.\n");
    printf("\t\t 5 - Everything.\n");
    
    printf("\n\n");
}

/**
 * void _shutdown()
 * 
 * Shuts down server. Inform all clients with SERVER_SHUTDOWN (without waiting for ACK),
 * frees all allocated memory, asks running threads to terminate and waits 
 * for them to finish.
 */
void _shutdown() {
    /* Inform clients about shutdown */
    char *msg = "SERVER_SHUTDOWN";    
    broadcast_clients(msg);
    
    /* Clear clients */
    clear_all_clients();
    /* Clear games */
    clear_all_games();
    
    log_line("SERV: Caught shutdown command.", LOG_ALWAYS);
    log_line("SERV: Asking threads to terminate.", LOG_ALWAYS);
    
    pthread_mutex_unlock(&mtx_thr_watchdog);
    pthread_mutex_unlock(&mtx_thr_receiver);
    pthread_mutex_unlock(&mtx_thr_sender);
    
    /* Join threads */
    pthread_join(thr_watchdog, NULL);
    pthread_join(thr_receiver, NULL);
    pthread_join(thr_sender, NULL);
    
    stop_logger();
}

/**
 * void run(int argc, char **argv)
 * 
 * Starts the server, processes command line arguments, initiates all necessary
 * functions and threads and enters infinite loop accepting user input.
 */
void run(int argc, char **argv) {
    char user_input_buffer[250];
    char addr_buffer[INET_ADDRSTRLEN] = {0};
    char *buff;
    struct in_addr tmp_addr;
    int port;
    int tmp_num;
    
    /* Init logger first */
    if(argc >= 4) {
        init_logger(argv[3]);
    }
    else {
        init_logger(STRINGIFY(DEFAULT_LOGFILE));
    }
    
    /* Validate input */
    if(argc >= 3) {
        
        /* Validate ip address */
        if(inet_pton(AF_INET, argv[1], (void *) &tmp_addr) <= 0 &&
                !hostname_to_ip(argv[1], addr_buffer)) {
            
            help();
            raise_error("Error validating server adress.\n");
            
        }
        
        if(!addr_buffer[0]) {
            strcpy(addr_buffer, argv[1]);
        }
        
        /* Validate port */
        port = (int) strtoul(argv[2], NULL, 10);
        
        if(port < 0 || port >= 65536) {
            help();
            raise_error("Port number is out of range.\n");
        }
        
        if(port <= 1024 && port != 0) {
            log_line("Trying to bind to a port number lower than 1024, this "
                    "might required administrator privileges.", LOG_ALWAYS);
        }
        
        /* Got log severity */
        if(argc >= 5) {
            log_level = (int) strtol(argv[4], NULL, 10);
        }
        
        sprintf(log_buffer,
                "Setting logging level to %d",
                log_level
                );
        
        log_line(log_buffer, LOG_ALWAYS);
        
        /* Got verbose */
        if(argc == 6) {
            verbose_level = (int) strtol(argv[5], NULL, 10);
        }
        
        sprintf(log_buffer,
                "Setting verbose level to %d",
                verbose_level
                );
        
        log_line(log_buffer, LOG_ALWAYS);
    }
    else {
        help();
        raise_error("Invalid arguments.\n");
    }
    
    /* Initiate server */
    init_server(addr_buffer, port);
    
    /* Start watchdog */
    pthread_mutex_init(&mtx_thr_watchdog, NULL);
    pthread_mutex_lock(&mtx_thr_watchdog);
    
    if(pthread_create(&thr_watchdog, NULL, start_watchdog, (void *) &mtx_thr_watchdog) != 0) {
        raise_error("Error starting watchdog thread.");
    }
    
    /* Start receiver */
    pthread_mutex_init(&mtx_thr_receiver, NULL);
    pthread_mutex_lock(&mtx_thr_receiver);
    
    if(pthread_create(&thr_receiver, NULL, start_receiving, (void *) &mtx_thr_receiver) != 0) {
        raise_error("Error starting receiving thread.");
    }
    
    /* Start sender */
    pthread_mutex_init(&mtx_thr_sender, NULL);
    pthread_mutex_lock(&mtx_thr_sender);
    
    if(pthread_create(&thr_sender, NULL, start_sending, (void *) &mtx_thr_sender) != 0) {
        raise_error("Error starting sender thread.");
    }
    
    /* Initiate server command line loop */
    while(1) {
        printf("CMD: ");
        
        if(fgets(user_input_buffer, 250, stdin) != NULL) {
            /* Exit server with exit, shutdown, halt or close commands */
            if( (strncmp(user_input_buffer, "exit", 4) == 0) ||
                    (strncmp(user_input_buffer, "shutdown", 8) == 0) ||
                    (strncmp(user_input_buffer, "halt", 4) == 0) ||
                    (strncmp(user_input_buffer, "close", 5) == 0)) {
                
                _shutdown();
                
                break;                
            }
            
            /* Set number that will be rolled */
            else if (strncmp(user_input_buffer, "force_roll", 10) == 0) {
                /* Strip header */
                if(strtok(user_input_buffer, " ") != NULL) {
                    buff = strtok(NULL, " ");
                    
                    if(buff != NULL) {
                        /* Get number */
                        force_roll = (int) strtoul(buff, NULL, 10);

                        if(force_roll >= 1 && force_roll <= 6) {
                            sprintf(log_buffer,
                                    "CMD: Forcing roll on all consequent rolls to %d",
                                    force_roll
                                    );
                            
                            log_line(log_buffer, LOG_ALWAYS);
                        }
                        else {
                            log_line("CMD: Rolling will be random now.", LOG_ALWAYS);
                        }
                    }
                }
            }
            
            /* Set log level */
            else if(strncmp(user_input_buffer, "set_log", 7) == 0) {
                if(strtok(user_input_buffer, " ") != NULL) {
                    buff = strtok(NULL, " ");
                    
                    if(buff) {
                        tmp_num = (int) strtoul(buff, NULL, 10);
                        
                        if(tmp_num >= LOG_NONE || tmp_num <= LOG_ALWAYS) {
                            log_level = tmp_num;
                            
                            sprintf(log_buffer,
                                    "CMD: Setting log level to %d",
                                    log_level
                                    );
                            
                            log_line(log_buffer, LOG_ALWAYS);
                        }
                    }
                }
            }
            
            /* Set log level */
            else if(strncmp(user_input_buffer, "set_verbose", 11) == 0) {
                if(strtok(user_input_buffer, " ") != NULL) {
                    buff = strtok(NULL, " ");
                    
                    if(buff) {
                        tmp_num = (int) strtoul(buff, NULL, 10);
                        
                        if(tmp_num >= LOG_NONE || tmp_num <= LOG_ALWAYS) {
                            verbose_level = tmp_num;
                            
                            sprintf(log_buffer,
                                    "CMD: Setting verbose level to %d",
                                    verbose_level
                                    );
                            
                            log_line(log_buffer, LOG_ALWAYS);
                        }
                    }
                }
            }
        }
    }
}

/**
 * int main(int argc, char **argv)
 * 
 * Entry point for whole program
 */
int main(int argc, char **argv) {
    run(argc, argv);
    
    return (EXIT_SUCCESS);
}