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
 * File: game.c
 * Description: Handles all game operations.
 * 
 * -----------------------------------------------------------------------------
 * 
 * @author: Martin Kucera, 2014
 * @version: 1.02
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "client.h"
#include "global.h"
#include "com.h"
#include "logger.h"

/* Logger buffer */
char log_buffer[LOG_BUFFER_SIZE];
/* If set to value between 1 and 6, all rolls will be this value */
int force_roll = -1;

/* Array with all created games */
game_t *games[MAX_CONCURRENT_CLIENTS];
/* Number of created games */
unsigned int game_num = 0;

/**
 * void generate_game_code(char *code, unsigned int iteration)
 * 
 * Generates unique game code with length specified by GAME_CODE_LEN
 */
void generate_game_code(char *code, unsigned int iteration) {
    game_t* existing_game;
    
    /* Prevent infinite loop */
    if(iteration > 100) {
        code[0] = 0;
        
        return;
    }
    
    gen_random(code, GAME_CODE_LEN);
    existing_game = get_game_by_code(code);
    
    if(existing_game != NULL) {
        release_game(existing_game);
        generate_game_code(code, iteration++);
    }
}

/**
 * game_t* get_game_by_code(char *code)
 * 
 * Attempts to find a game with given code. If game is found,
 * it's mutex will be locked in order to prevent any changes by other threads,
 * but has to be released manually in order to prevent a deadlock.
 */
game_t* get_game_by_code(char *code) {
    int i;
    
    for(i = 0; i < MAX_CONCURRENT_CLIENTS; i++) {
        if(games[i]) {
            pthread_mutex_lock(&games[i]->mtx_game);

            if(strncmp(code, games[i]->code, GAME_CODE_LEN) == 0) {
                return games[i];
            }

            pthread_mutex_unlock(&games[i]->mtx_game);
        }
    }
    
    return NULL;
}

/**
 * game_t* get_game_by_index(unsigned int index)
 * 
 * Checks if game at given index exists, if so, returns a pointer to that game
 * with locked mutex. Mutex has to be released manually.
 */
game_t* get_game_by_index(unsigned int index) {
    if(index>= 0 && MAX_CONCURRENT_CLIENTS > index) {
        if(games[index]) {
                pthread_mutex_lock(&games[index]->mtx_game);
        
                return games[index];
        }
    }
    
    return NULL;
}

/**
 * void release_game(game_t *game)
 * 
 * Checks if the mutex of given game is locked, if so, unlocks it.
 */
void release_game(game_t *game) {
    if(pthread_mutex_trylock(&game->mtx_game) != 0) {
        pthread_mutex_unlock(&game->mtx_game);
    }
    else {
        log_line("Tried to release non-locked game", LOG_WARN);
    }
}

/**
 * void create_game(client_t *client)
 * 
 * Creates new game and notifies client with it's code. Client is not notified
 * with the JOINED_GAME packet, but rather with GAME_CREATED following with
 * the game code.
 * 
 * There is no need to check if the game number is higher than allowed, because
 * maximum allowed number of games is the same as maximum number of connected
 * clients and each client can be present only in one game.
 */
void create_game(client_t *client) {
    char *message;
    char buff[11];
    unsigned int message_len;
    int i = 0;
    
    game_t *game = (game_t *) malloc(sizeof(game_t));
    
    /* Update game timestamp */
    gettimeofday(&game->timestamp, NULL);
    game->state = 0;
    game->player_num = 1;
    
    pthread_mutex_init(&game->mtx_game, NULL);
    
    memset(game->player_index, -1, sizeof(int) * 4);
    game->player_index[0] = client->client_index;
    
    game->code = (char *) malloc(GAME_CODE_LEN + 1);    
    generate_game_code(game->code, 0);
    
    if(game->code[0] != 0) {        
        /* Find empty game index */
        for(i = 0; i < MAX_CONCURRENT_CLIENTS; i++) {
            if(games[i] == NULL) {
                games[i] = game;
                game->game_index = i;

                break;
            }
        }
        
        game_num++;
        
        /* Place figures at their starting position */
        for(i = 0; i < 16; i++) {
            game->game_state.figures[i] = 56 + i;
            game->game_state.fields[56 + i] = i;
        }
        
        /* Set fields to empty */
        memset(game->game_state.fields, -1, (72 * sizeof(int)));        
        /* Set finished positions to empty */
        memset(game->game_state.finished, -1, (4 * sizeof(short int)));
        
        game->game_state.playing_rolled_times = 0;
        game->game_state.playing_rolled = -1;
        
        /* Set invalid next playing on purpose */
        game->game_state.playing = 100;
        
        /* Prepare message for client */
        sprintf(buff, "%d", ( GAME_MAX_LOBBY_TIME_SEC - 1));
        message_len = strlen(buff) + GAME_CODE_LEN + 14 + 1;
        message = (char *) malloc(message_len);
        
        sprintf(message, "GAME_CREATED;%s;%d", game->code, GAME_MAX_LOBBY_TIME_SEC - 1);
        
        enqueue_dgram(client, message, 1);
        
        /* Log */
        sprintf(log_buffer,
                "Created new game with code %s and index %d",
                game->code,
                game->game_index
                );
        
        log_line(log_buffer, LOG_DEBUG);
        
        /* Set clients game code */
        client->game_index = game->game_index;
        
        free(message);
    }    
}

/**
 * void send_game_state(client_t *client, game_t *game)
 * 
 * Send's current game state to client, usually after joining. Informs client
 * which state the game is in (waiting, running), which players are connected,
 * at which positions are their figures, currently playing index, game index
 * of current client and timeout.
 * 
 * This could be used for clients to rejoin games which they were disconnected from,
 * but is not currently supported.
 */
void send_game_state(client_t *client, game_t *game) {
    char *buff;
    unsigned short release = 0;
    unsigned short player[4] = {0};
    unsigned int i;
    int client_game_index;
    client_t *cur_client;
    
    if(client != NULL) {
        if(game == NULL) {
            game = get_game_by_index(client->game_index);
            
            release = 1;
        }
        
        if(game) {
            /* If client is in that game */
            if(game->game_index == client->game_index) {
                /* Buffer is set to maximum possible size, but the actual message
                 * is terminated by 0 so client can get the actual length
                 */
                buff = (char *) malloc(105 + GAME_CODE_LEN + 11);

                /* Get players that are playing */
                for(i = 0; i < 4; i++) {
                    if(game->player_index[i] != -1) {

                        if(game->player_index[i] == client->client_index) {
                            client_game_index = i;

                            player[i] = 1;
                        }
                        else {
                            cur_client = get_client_by_index(game->player_index[i]);

                            if(cur_client) {
                                /* Client is active */
                                if(cur_client->state) {
                                    player[i] = 1;
                                }
                                /* Client timeouted */
                                else {
                                    player[i] = 2;
                                }

                                /* Release client */
                                release_client(cur_client);
                            }
                        }
                    }
                }

                /* game code, game state, 4x player connected, 16x figure position,
                 * index of currently playing client, game index of connecting player
                 * and timeout before next state change (lobby timeout, playing timeout)
                 */
                sprintf(buff,
                        "GAME_STATE;%s;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%u;%d;%d",
                        game->code,
                        game->state, 
                        player[0],
                        player[1],
                        player[2],
                        player[3],
                        game->game_state.figures[0],
                        game->game_state.figures[1],
                        game->game_state.figures[2],
                        game->game_state.figures[3],
                        game->game_state.figures[4],
                        game->game_state.figures[5],
                        game->game_state.figures[6],
                        game->game_state.figures[7],
                        game->game_state.figures[8],
                        game->game_state.figures[9],
                        game->game_state.figures[10],
                        game->game_state.figures[11],
                        game->game_state.figures[12],
                        game->game_state.figures[13],
                        game->game_state.figures[14],
                        game->game_state.figures[15],
                        game->game_state.playing,
                        client_game_index,
                        game_time_before_timeout(game),
                        game->game_state.playing_rolled
                        );

                enqueue_dgram(client, buff, 1);

                free(buff);
            }
            
            if(release) {
                /* Release game */
                release_game(game);
            }
        }
    }
}

/**
 * void remove_game(game_t **game, client_t *skip)
 * 
 * Removes game and sets it's pointer to NULL
 */
void remove_game(game_t **game, client_t *skip) {
    int i;
    client_t *cur;
    
    if(game != NULL) {
        /* Log */
        sprintf(log_buffer,
                "Removing game with code %s and index %d",
                (*game)->code,
                (*game)->game_index
                );
        
        log_line(log_buffer, LOG_DEBUG);
        
        /* Set all player's game index to - 1 */
        for(i = 0; i < 4; i++) {
            if((*game)->player_index[i] != -1 && 
                    (!skip || (*game)->player_index[i] != skip->client_index)) {
                
                cur = get_client_by_index((*game)->player_index[i]);
                
                if(cur) {
                    cur->game_index = -1;
                    
                    /* Release client */
                    release_client(cur);
                }
            }
        }
        
        free((*game)->code);
        
        games[(*game)->game_index] = NULL;
        game_num--;
        
        release_game((*game));
        
        free((*game));
    }
    
    *game = NULL;
}

/**
 * void broadcast_game(game_t *game, char *msg, client_t *skip, int send_skip)
 * 
 * Sends message to all clients (players) connected to a game.
 */
void broadcast_game(game_t *game, char *msg, client_t *skip, int send_skip) {
    int i;
    client_t *client;
    int release;
    
    if(game != NULL) {        
        for(i = 0; i < 4; i++) {
            client = NULL;
            release = 1;
            
            /* Player exists */
            if(game->player_index[i] != -1) {                
                /* If we want to skip client */
                if(!skip || game->player_index[i] != skip->client_index) {
                    client = get_client_by_index(game->player_index[i]);
                }
                else if(skip && send_skip) {
                    client = skip;
                    release = 0;
                }
                
                if(client != NULL) {
                    if(client->state) {
                        enqueue_dgram(client, msg, 1);
                    }
                    
                    if(release) {
                        /* Release client */
                        release_client(client);
                    }
                }
                
            }
        }
    }
}

/**
 * void join_game(client_t *client, char* game_code)
 * 
 * Tries to join a game with given code, if unsuccessful informs
 * client what was the problem.
 */
void join_game(client_t *client, char* game_code) {
    int i;
    game_t *game;
    char buff[21];
    
    /* Check if client is already in a game */
    if(client->game_index == -1) {
        game = get_game_by_code(game_code);
    }
    
    if(game != NULL) {
        if(!game->state) {
            /* If we have a spot */
            if(game->player_num < 4) {

                /* Find spot for player */
                for(i = 0; i < 4; i++) {
                    if(game->player_index[i] == -1) {
                        game->player_index[i] = client->client_index;

                        break;
                    }
                }

                /* Prepare message */
                strcpy(buff, "CLIENT_JOINED_GAME;");
                buff[19] = (char) (((int) '0') + i);
                buff[20] = 0;

                /* Set clients game index reference to this game */
                client->game_index = game->game_index;

                /* Send game state to joined client */
                send_game_state(client, game);

                /* Broadcast game, skipping current client */
                broadcast_game(game, buff, client, 1);

                game->player_num++;
                
                /* Log */
                sprintf(log_buffer,
                        "Player with index %d joined game with code %s and index %d",
                        client->client_index,
                        game->code,
                        game->game_index
                        );
                
                log_line(log_buffer, LOG_DEBUG);

            }
            /* Game is full */
            else {
                /* Log */
                sprintf(log_buffer,
                        "Client with index %d tried to join game with code %s and index %d, but game was full",
                        client->client_index,
                        game->code,
                        game->game_index
                        );
                
                log_line(log_buffer, LOG_DEBUG);
                
                enqueue_dgram(client, "GAME_FULL", 1);
            }
        }
        /* Game is already running */
        else {
            /* Log */
            sprintf(log_buffer,
                    "Client with index %d tried to join game with code %s and index %d, but game was already running",
                    client->client_index,
                    game->code,
                    game->game_index
                    );

            log_line(log_buffer, LOG_DEBUG);
            
            enqueue_dgram(client, "GAME_RUNNING", 1);
        }
        
        /* Release game */
        release_game(game);
    }
    /* Non existent game, inform user */
    else {
        /* Log */
        sprintf(log_buffer,
                "Client with index %d tried to join game with code %s, but game DOESNT EXIST",
                client->client_index,
                game_code
                );

        log_line(log_buffer, LOG_DEBUG);
        
        enqueue_dgram(client, "GAME_NONEXISTENT", 1);
    }
}

/**
 * void leave_game(client_t *client)
 * 
 * If given client is in a game, removes him from the game and possibly notifies
 * other players in the same game.
 */
void leave_game(client_t *client) {
    game_t *game;
    int i, n;
    int len;
    char *buff;
    
    if(client != NULL) {
        game = get_game_by_index(client->game_index);
        
        if(game != NULL) {
            /* Log */
            sprintf(log_buffer,
                    "Client with index %d is leaving game with code %s and index %d",
                    client->client_index,
                    game->code,
                    game->game_index
                    );
            
            log_line(log_buffer, LOG_DEBUG);
            
            if(game->player_num == 1) {
                
                remove_game(&game, client);
                
            }
            else {
                
                for(i = 0; i < 4; i++) {
                    /* If client index matches */
                    if(game->player_index[i] == client->client_index) {
                        game->player_index[i] = -1;
                        game->player_num--;
                        
                        /* If game is running */
                        if(game->state) {
                            /* Reset clients figures */
                            for(n = 4 * i; n < (4 * i) + 4; n++) {
                                /* Place figures at their starting position */
                                game->game_state.figures[n] = 56 + n;
                                game->game_state.fields[56 + n] = -1;
                            }

                            /* If leaving player was supposed to play next */
                            if(game->game_state.playing == i) {
                                /* Find next player that will be playing */
                                set_game_playing(game);
                            }
                        }
                        
                        break;
                    }
                }
                
                len = 30 + 11;
                buff = (char *) malloc(len);
                                
                /* @TODO: if he wasnt playing do something else */
                /* Notify other players that one left */
                sprintf(buff, 
                        "CLIENT_LEFT_GAME;%d;%d;%d", 
                        i, 
                        game->game_state.playing,
                        GAME_MAX_PLAY_TIME_SEC - 1
                        );
                
                broadcast_game(game, buff, NULL, 0);
                
                free(buff);
            }
            
            /* Reset client's game code */
            client->game_index = -1;
            
            enqueue_dgram(client, "GAME_LEFT", 1);
            
            if(game != NULL) {
                /* Release game */
                release_game(game);
            }
        }
    }
}

/**
 * int timeout_game(client_t *client)
 * 
 * Changes game state if one of the players timeouts. He has a maximum amount of  
 * time set by MAX_CLIENT_TIMEOUT_SEC to reconnect
 */
int timeout_game(client_t *client) {
    int i;
    char buff[40];
    game_t *game = get_game_by_index(client->game_index);
    
    if(game) {
        if(game->state) {
            /* Log */
            sprintf(log_buffer,
                    "Client with index %d timeouted from game with code %s and index %d, can reconnect",
                    client->client_index,
                    game->code,
                    game->game_index
                    );

            log_line(log_buffer, LOG_DEBUG);

            /* Wont wait for timeouted player if he is alone in game */
            if(game->player_num > 1) {
                for(i = 0; i < 4; i++) {
                    if(game->player_index[i] == client->client_index) {
                        break;
                    }
                }
                
                if(game->game_state.playing == i) {
                    set_game_playing(game);
                }
                
                sprintf(buff, 
                        "CLIENT_TIMEOUT;%d;%d;%d", 
                        i, 
                        game->game_state.playing,
                        GAME_MAX_PLAY_TIME_SEC - 1
                        );
                
                /* Mark client as inactive */
                client->state = 0;
                
                broadcast_game(game, buff, client, 0);
                
                /* Release game */
                release_game(game);
                
                /* Update clients timestamp (starts countdown for max timeout time) */
                update_client_timestamp(client);

                return 1;
            }
            else {
                remove_game(&game, client);
            }
        }
    }
    
    return 0;
}

/**
 * void start_game(client_t *client)
 * 
 * Attempts to start a game given client is in. Game has to be in state 0 (waiting),
 * if game successfully started, informs all connected players.
 */
void start_game(client_t *client) {
    game_t *game = NULL;
    char *buff;
    int i;
    
    if(client->game_index != -1) {
        game = get_game_by_index(client->game_index);
        
        if(game) {                
            if(!game->state && game->player_num > 0 && !all_players_finished(game)) {  
                /* Log */
                sprintf(log_buffer,
                        "Client with index %d started game with code %s and index %d",
                        client->client_index,
                        game->code,
                        game->game_index
                        );

                log_line(log_buffer, LOG_DEBUG);

                game->state = 1;

                /* Set which first client as playing */
                for(i = 0; i < 4; i++) {
                    if(game->player_index[i] != -1) {
                        game->game_state.playing = i;

                        break;
                    }
                }

                /* Player can have 3 tries to roll 6 */
                game->game_state.playing_rolled_times = 0;

                /* Broadcast clients */
                /* GAME_MAX_LOBBY_TIME_SEC is expected to be bigger */
                buff = (char *) malloc(16 + 11);
                sprintf(buff, 
                        "GAME_STARTED;%d;%d", 
                        game->game_state.playing,
                        GAME_MAX_PLAY_TIME_SEC
                        );

                broadcast_game(game, buff, client, 1);

                /* Free buffer */
                free(buff);          

                /* Update game timestamp */
                gettimeofday(&game->timestamp, NULL);
                /* Update game state timestamp */
                gettimeofday(&game->game_state.timestamp, NULL);

            }
        
        /* Release game */
        release_game(game);
        }
    }
}

/**
 * void set_game_playing(game_t *game)
 * 
 * Chooses the next player from game that will be playing (able to roll)
 */
void set_game_playing(game_t *game) {
    int cur = game->game_state.playing;
    int i = 0;
    client_t *client;
    
    /* Reset rolled number */
    game->game_state.playing_rolled = -1;
    
    if(cur == 100) {
        cur = 0;
    }
    
    if(game->player_num > 1) {
        for(i = 1; i < 4; i++) {
            if(game->player_index[(cur + i) % 4] != -1 &&
                    get_player_finish_pos(game, ((cur + i) % 4)) == -1) {
                
                client = get_client_by_index(game->player_index[(cur + i) % 4]);
                
                if(client) {
                    if(client->state) {
                        game->game_state.playing = (cur + i ) % 4;
                        
                        /* Release client */
                        release_client(client);
                        
                        break;
                    }
                    
                    /* Release client */
                    release_client(client);
                }
                        
            }
        }
    }
    
    
    if(player_has_figures_on_field(game, game->game_state.playing)) {
        game->game_state.playing_rolled_times = 3;
    }
    else {
        game->game_state.playing_rolled_times = 0;
    }
    
    /* Update game timestamp */
    gettimeofday(&game->timestamp, NULL);
}

/**
 * int player_has_figures_on_field(game_t *game, unsigned int player_index)
 * 
 * Checks if given player index has any figures on field, or all his figures
 * are at start.
 */
int player_has_figures_on_field(game_t *game, unsigned int player_index) {
    int i;
    int base = (4 * player_index);
    
    for(i = 0; i < 4; i++) {
        if(game->game_state.figures[(base + i)] >= 0 && 
                game->game_state.figures[(base + i)] <= 55) {
            
            return 1;
            
        }
    }
    
    return 0;
}

/**
 * int game_time_play_state_timeout(game_t *game)
 * 
 * Checks if given game timeouted in running state (player didnt roll for
 * maximum allowed time)
 */
int game_time_play_state_timeout(game_t *game) {
    struct timeval cur_tv;
    gettimeofday(&cur_tv, NULL);
    
    return ( (GAME_MAX_PLAY_STATE_TIME_SEC - (cur_tv.tv_sec - game->game_state.timestamp.tv_sec) ) <= 0);
}

/**
 * int game_time_before_timeout(game_t *game)
 * 
 * Returns how many seconds are left before game timeouts considering it's state
 */
int game_time_before_timeout(game_t *game) {
    struct timeval cur_tv;    
    gettimeofday(&cur_tv, NULL);
    
    /* Game running */
    if(game->state) {
        return (GAME_MAX_PLAY_TIME_SEC - (cur_tv.tv_sec - game->timestamp.tv_sec));
    }
    /* Game in lobby */
    else {
        return (GAME_MAX_LOBBY_TIME_SEC - (cur_tv.tv_sec - game->timestamp.tv_sec));
    }
}

/**
 * void roll_die(client_t *client)
 * 
 * If force_roll is set, sets that number to rolled, otherwise rolls
 * a random number from range 0 to 6 and notifies all players connected
 * to the same game as client. Also checks if current player can make a move
 * with any of his figures, if not, decides which player will be playing next.
 */
void roll_die(client_t *client) {
    int rolled;
    game_t *game = get_game_by_index(client->game_index);
    char buff[13];
    
    if(game) {
        /* Game is running and client is playing */
        if(game->state && 
                game->player_index[game->game_state.playing] == client->client_index &&
                game->game_state.playing_rolled == -1) {
            
            if(force_roll >= 1 && force_roll <= 6) {
                rolled = force_roll;
            }
            else {            
                rolled = rand_lim(6);
            }
            
            game->game_state.playing_rolled = rolled;
            game->game_state.playing_rolled_times++;
            
            /* Send client which number he rolled */
            sprintf(buff, "ROLLED_DIE;%d", rolled);
            broadcast_game(game, buff, client, 1);
            
            /* Log */
            sprintf(log_buffer,
                    "Client with index %d rolled number %d",
                    client->client_index,
                    rolled
                    );
            
            log_line(log_buffer, LOG_DEBUG);
            
            /* Player hs no figures that can move */
            if(!can_player_play(game, game->game_state.playing)) {
                
                /* Can player play again? */
                if( (rolled != 6 ) && 
                        ( (player_has_figures_on_field(game, game->game_state.playing)) ||
                        ( game->game_state.playing_rolled_times >= 3 ) ) ) {
                    
                    set_game_playing(game);
                    
                }
                
                broadcast_game_playing_index(game, client);
                
                /* Reset rolled number */
                game->game_state.playing_rolled = -1;
            }
            
            /* Update game state timestamp */
            gettimeofday(&game->game_state.timestamp, NULL);
        }
        /* Error, reload client state */
        else {
            send_game_state(client, game);
        }
        
        /* Release game */
        release_game(game);
    }
}

/**
 * void broadcast_game_playing_index(game_t *game, client_t *skip)
 * 
 * Notifies all players in given game which player will be playing next
 */
void broadcast_game_playing_index(game_t *game, client_t *skip) {
    char *buff = get_playing_index_message(game);
    
    broadcast_game(game, buff, skip, 1);
    
    free(buff);
}

/**
 * char* get_playing_index_message(game_t *game)
 * 
 * Creates message which informs players, who is playing
 */
char* get_playing_index_message(game_t *game) {
    char *buff, play_time_buff[11];
    int len;
    
    sprintf(play_time_buff, "%u", GAME_MAX_PLAY_TIME_SEC);
    
    len = (17 + strlen(play_time_buff));
    buff = (char *) malloc(len);
    
    sprintf(buff,
            "PLAYING_INDEX;%d;%d",
            game->game_state.playing,
            GAME_MAX_PLAY_TIME_SEC
            );
    
    return buff;
}

/**
 * int can_player_play(game_t *game, unsigned int player_index) 
 * 
 * Checks if given player can play (any of his figures can actually move)
 */
int can_player_play(game_t *game, unsigned int player_index) {
    int base = 4 * player_index;
    int i;
    
    for(i = 0; i < 4; i++) {
        if(can_figure_move(game, (base + i), NULL)) {
            return 1;
        }
    }
    
    return 0;
}

/**
 * int can_figure_move(game_t *game, unsigned int figure_index, unsigned int *d_index)
 * 
 * Checks if figure with given index can move to by number of fields that is
 * kept at game's state. If so and d_index isn't NULL, returns destination field.
 */
int can_figure_move(game_t *game, unsigned int figure_index, unsigned int *d_index) {
    int figure_field_index;
    int move_by;
    int dest_index = -1;
    int diff;
    int figure_player_index;
    int retval = 0;
    
    /* Game not running */
    if(!game->state) {
        return 0;
    }
    
    figure_field_index = game->game_state.figures[figure_index];
    move_by = game->game_state.playing_rolled;
    
    /* Green figure */
    if(figure_index >= 0 && figure_index <= 3) {
        /* Moving on field */
        if( (figure_field_index + move_by) <= 39 ||
                ( ( figure_field_index + move_by ) >= 40 && 
                (figure_field_index + move_by) <= 43 ) ) {
            
            dest_index = figure_field_index + move_by;
            
        }
        /* Moving from start */
        else if(figure_field_index >= 56 && figure_field_index <= 59 && 
                move_by == 6) {
             
            dest_index = 0;
            
        }
    }
    /* Blue figure */
    else if(figure_index >= 4 && figure_index <= 7) {
        /* Moving on field */
        if( (figure_field_index >= 10 && figure_field_index <= 39) ||
                ( figure_field_index < 39 && (figure_field_index + move_by % 40 <= 9) ) ) {
            
            dest_index = (figure_field_index + move_by) % 40;
            
        }
        /* Moving to/in blue home */
        else {
            /* Moving to blue home */
            if(figure_field_index <= 9) {
                diff = (figure_field_index + move_by) - 9 - 1;
                
                if(44 + diff <= 47) {
                    dest_index = 44 + diff;
                }
            }
            /* Moving in blue home */
            else if(figure_field_index >= 44 && figure_field_index <= 47 &&
                    ( figure_field_index + move_by <= 47 ) ) {
                
                dest_index = figure_field_index + move_by;
                
            }
            /* Moving from start */
            else if(figure_field_index >= 60 && figure_field_index <= 63 &&
                    move_by == 6) {
                
                dest_index = 10;
                
            }
        }
    }
    /* Yellow figure */
    else if(figure_index >= 8 && figure_index <= 11) {
        /* Moving on field */
        if( (figure_field_index >= 20 && figure_field_index <= 39) ||
                ( figure_field_index < 39 && (figure_field_index + move_by % 40 <= 19) ) ) {
            
            dest_index = (figure_field_index + move_by) % 40;
            
        }
        /* Moving to/in blue home */
        else {
            /* Moving to blue home */
            if(figure_field_index <= 19) {
                diff = (figure_field_index + move_by) - 19 - 1;
                
                if(48 + diff <= 51) {
                    dest_index = 48 + diff;
                }
            }
            /* Moving in blue home */
            else if(figure_field_index >= 48 && figure_field_index <= 51 &&
                    ( figure_field_index + move_by <= 51 ) ) {
                
                dest_index = figure_field_index + move_by;
                
            }
            /* Moving from start */
            else if(figure_field_index >= 64 && figure_field_index <= 67 &&
                    move_by == 6) {
                
                dest_index = 20;
                
            }
        }
    }
    /* Red figure */
    else if(figure_index >= 12 && figure_index <= 15) {
        /* Moving on field */
        if( (figure_field_index >= 30 && figure_field_index <= 39) ||
                ( figure_field_index < 39 && (figure_field_index + move_by % 40 <= 29 )) ) {
            
            dest_index = (figure_field_index + move_by) % 40;
            
        }
        /* Moving to/in blue home */
        else {
            /* Moving to blue home */
            if(figure_field_index <= 29) {
                diff = (figure_field_index + move_by) - 29 - 1;
                
                if(52 + diff <= 55) {
                    dest_index = 52 + diff;
                }
            }
            /* Moving in blue home */
            else if(figure_field_index >= 52 && figure_field_index <= 55 &&
                    ( figure_field_index + move_by <= 55 ) ) {
                
                dest_index = figure_field_index + move_by;
                
            }
            /* Moving from start */
            else if(figure_field_index >= 68 && figure_field_index <= 71 &&
                    move_by == 6) {
                
                dest_index = 30;
                
            }
        }
    }
    
    if(dest_index != -1) {
        figure_player_index = (int) (figure_index / 4.);
        
        if(d_index) {
            *d_index = dest_index;
        }
        
        switch(figure_player_index) {
            case 0:
                if(game->game_state.fields[dest_index] == -1 ||
                        game->game_state.fields[dest_index] > 3) {
                    
                    retval = 1;
                    
                }
                break;
                
            case 1:
                if(game->game_state.fields[dest_index] == -1 ||
                        game->game_state.fields[dest_index] < 4 ||
                        game->game_state.fields[dest_index] > 7) {
                    
                    retval = 1;
                    
                }
                
                break;
                
            case 2:
                if(game->game_state.fields[dest_index] == -1 ||
                        game->game_state.fields[dest_index] < 8 ||
                        game->game_state.fields[dest_index] > 11) {
                    
                    retval = 1;
                    
                }
                
                break;
                
            case 3:
                if(game->game_state.fields[dest_index] == -1 ||
                        game->game_state.fields[dest_index] < 12 ) {
                    
                    retval = 1;
                    
                }
                
                break;
        }
    }
    
    return retval;
}

/**
 * void move_figure(client_t *client, unsigned int figure_index)
 * 
 * Moves figure by a number of fields that is set at client's game state.
 * Notifies all players that figure moved and checks if this move
 * was the last for current player and possibly for whole game. If game ended,
 * sends notification to all players with standings.
 */
void move_figure(client_t *client, unsigned int figure_index) {
    game_t *game;
    unsigned int dest_index;
    int removed_figure;
    int i;
    char *buff;
    
    if(client && client->game_index != -1) {
        game = get_game_by_index(client->game_index);
        
        if(game) {
            /* Check game state */
            if(game->state && game->game_state.playing != -1) {
                /* Check if client is actually playing and did already roll  */
                if(game->player_index[game->game_state.playing] == client->client_index &&
                        game->game_state.playing_rolled != -1) {
                    
                    /* Check if moving figure belongs to our client */
                    if( ( figure_index >= ( 4 * game->game_state.playing ) ) &&
                            ( figure_index <= (4 * game->game_state.playing + 3) )) {
                        
                        /* Check if figure can move by given number */
                        if(can_figure_move(game, figure_index, &dest_index)) {
                            
                            buff = (char *) malloc(19);
                            
                            /* Update positions */
                            if(game->game_state.fields[dest_index] != -1) {
                                /* Get index of removed figure */
                                removed_figure = game->game_state.fields[dest_index];
                                
                                /* Update figures field to empty home spot */
                                game->game_state.figures[removed_figure] = find_home(removed_figure);
                                /* Link back from field to figure */
                                game->game_state.fields[game->game_state.figures[removed_figure]] = removed_figure;
                                
                                sprintf(buff,
                                        "FIGURE_MOVED;%d;%d",
                                        removed_figure,
                                        game->game_state.figures[removed_figure]
                                        );

                                /* Broadcast game */
                                broadcast_game(game, buff, client, 1);
                            }
                            
                            game->game_state.fields[game->game_state.figures[figure_index]] = -1;
                            game->game_state.figures[figure_index] = dest_index;
                            game->game_state.fields[dest_index] = figure_index;
                                                        
                            /* Prepare buffer */
                            sprintf(buff, 
                                    "FIGURE_MOVED;%u;%u",
                                    figure_index,
                                    dest_index
                                    );

                            /* Broadcast game */
                            broadcast_game(game, buff, client, 1);
                            
                            free(buff);
                            
                            /* Log */
                            sprintf(log_buffer,
                                    "Client with index %d moved figure to field %d",
                                    client->client_index,
                                    dest_index
                                    );
                            
                            log_line(log_buffer, LOG_DEBUG);
                            
                            /* Check if player finished */
                            if(dest_index >= 40 && has_all_figures_at_home(game, game->game_state.playing)) {
                                for(i = 0; i < 4; i++) {
                                    if(game->game_state.finished[i] == -1) {
                                        /* Log */
                                        sprintf(log_buffer,
                                                "Client with index %d in game with code %s and index %d finished",
                                                client->client_index,
                                                game->code,
                                                game->game_index
                                                );
                                        
                                        log_line(log_buffer, LOG_DEBUG);
                                        
                                        game->game_state.finished[i] = game->game_state.playing;
                                        
                                        break;
                                    }
                                }
                            }
                            
                            /* Game is over */
                            if(dest_index >= 40 && all_players_finished(game)) {      
                                /* Log */
                                sprintf(log_buffer,
                                        "All players in game with code %s and index %d finished",
                                        game->code,
                                        game->game_index
                                        );
                                
                                log_line(log_buffer, LOG_DEBUG);
                                
                                broadcast_game_finish(game, client);
                                
                                game->state = 0;
                            }
                            /* Game still running */
                            else {
                                /* If player didnt roll 6 another gets to play or 
                                 * if player has figures on field or player doesnt
                                 * have figures on field but rolled 3 times already
                                 */
                                if( (game->game_state.playing_rolled != 6 ) && 
                                        ( (player_has_figures_on_field(game, game->game_state.playing)) ||
                                        ( game->game_state.playing_rolled_times >= 3 ) ) ) {

                                    set_game_playing(game);

                                }

                                buff = get_playing_index_message(game);

                                /* Broadcast game */
                                broadcast_game(game, buff, client, 1);

                                free(buff);

                                /* Reset rolled number */
                                game->game_state.playing_rolled = -1;

                                /* Update game timestamp */
                                gettimeofday(&game->timestamp, NULL);
                                /* Update game state timestamp */
                                gettimeofday(&game->game_state.timestamp, NULL);
                            }
                            
                        }
                        /* @TODO: send game_state */

                    }
                    /* @TODO: send_game state */

                }
                /* @TODO: send game_state */
            }
            /* @TODO: send game_state */
            
            /* Release game */
            release_game(game);
        }
    }
}

/**
 * int find_home(int figure_index)
 * 
 * Returns starting field index of given figure
 */
int find_home(int figure_index) {    
    return (figure_index + 56);
}

/**
 * int get_player_finish_pos(game_t *game, int index)
 * 
 * Get position at which player finished, if he didnt' finish
 * yet, returns -1
 */
int get_player_finish_pos(game_t *game, int index) {
    int i;
    
    for(i = 0; i < 4; i++) {
        if(game->game_state.finished[i] == index) {
            return i;
        }
    }
    
    return -1;
}

/**
 * int all_players_finished(game_t *game)
 * 
 * Checks if all players in given game finished. Game is considered over if
 * only there is only one MORE player that hasnt finished yet, meaning if there
 * are 3 players and 2 of them finished, game is over.
 */
int all_players_finished(game_t *game) {
    int i;
    int unfinished = 0;
    int unfinished_index = -1;
    
    printf("Entering player finished..\n");
    
    for(i = 0; i < 4; i++) {
        /* Check if player exists and if he's marked as finished */
        if(game->player_index[i] != -1 &&
                get_player_finish_pos(game, i) == -1) {
            
            unfinished++;
            unfinished_index = i;
            
            if(game->player_num == 1 || unfinished > 1) {
                return 0;
            }
            
        }
    }
    
    printf("Leaving player finished..\n");
    
    if(unfinished_index != -1) {
        game->game_state.finished[game->player_num - 1] = unfinished_index;
    }
    
    return 1;
}

/**
 * int has_all_figures_at_home(game_t *game, int player_index)
 * 
 * Checks if player has all his figures at home fields.
 */
int has_all_figures_at_home(game_t *game, int player_index) {
    int i, base_fig, base_field;
    
    base_fig = 4 * player_index;
    base_field = 40 + base_fig;
    
    for(i = 0; i < 4; i++) {
        
        if(game->game_state.figures[base_fig + i] < (base_field) || 
                game->game_state.figures[base_fig + i] > (base_field + 3)) {
            
            return 0;
            
        }
    }
    
    return 1;
}

/**
 * void broadcast_game_finish(game_t *game, client_t *skip)
 * 
 * Informs all players in game that game finished. Also sends
 * standings for each player.
 */
void broadcast_game_finish(game_t *game, client_t *skip) {
    char msg[22];
    
    sprintf(msg,
            "GAME_FINISHED;%d;%d;%d;%d",
            get_player_finish_pos(game, 0),
            get_player_finish_pos(game, 1),
            get_player_finish_pos(game, 2),
            get_player_finish_pos(game, 3)
            );
    
    broadcast_game(game, msg, skip, 1);
}

/**
 * void clear_all_games()
 * 
 * Removes (and frees) all games.
 */
void clear_all_games() {
    int i = 0;
    game_t *game;
    
    for(i = 0; i < MAX_CONCURRENT_CLIENTS; i++) {
        game = get_game_by_index(i);
        
        if(game) {
            games[i] = NULL;
            free(game->code);
            
            pthread_mutex_unlock(&game->mtx_game);
            free(game);
        }
    }
}
