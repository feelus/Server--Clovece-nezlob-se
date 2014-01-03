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
 * @version: 1.0
 * 
 */

#ifndef GAME_H
#define	GAME_H

#include <pthread.h>
#include <sys/time.h>

#include "client.h"

extern unsigned int game_num;
extern int force_roll;

typedef struct {
    /* (0 - 39) game fields,  (40 - 43) green homes,
     * (44 - 47) blue homes, (48 - 51) yellow homes, 
     * (52 - 55) red homes
     */
    
    /* Game fields (not necessary, but easier to check if field is empty) */
    unsigned int fields[72];
    
    /* Game figures (green, blue, yellow, red) */
    int figures[16];
    
    /* Turn to play */
    unsigned short playing;
    
    /* Player playing current rolled number */
    short playing_rolled;
    
    /* If player has no figures on field but only in start, how many times
     * he rolled (can 3x)
     */
    short playing_rolled_times;
    
    /* Last time someone actually played */
    struct timeval timestamp;
    
    /* Finished players position */
    short int finished[4];
    
} game_state_t;

typedef struct {
    /* Game mutex */
    pthread_mutex_t mtx_game;
    /* Game index */
    unsigned int game_index;
    
    /* Game state - 1 running, 0 waiting */
    unsigned short state;
    /* Player count */
    unsigned short player_num;    
    
    /* Game code (used to identify games) */
    char *code;
    
    /* Addresses of connected players */
    int player_index[4];
    
    /* Current game's game state */
    game_state_t game_state;
    
    /* Last game update */
    struct timeval timestamp;
    
} game_t;

/* Function prototypes */
void generate_game_code(char *code, unsigned int iteration);
game_t* get_game_by_code(char *code);
game_t* get_game_by_index(unsigned int index);
void release_game(game_t *game);
void create_game(client_t *client);
void send_game_state(client_t *client, game_t *game);
void remove_game(game_t **game, client_t *client);
void broadcast_game(game_t *game, char *msg, client_t *skip, int send_skip);
void join_game(client_t *client, char* game_code);
void leave_game(client_t *client);
void start_game(client_t *client);
void set_game_playing(game_t *game);
int player_has_figures_on_field(game_t *game, unsigned int player_index);
int game_time_play_state_timeout(game_t *game);
int game_time_before_timeout(game_t *game);
void roll_die(client_t *client);
void broadcast_game_playing_index(game_t *game, client_t *skip);
char* get_playing_index_message(game_t *game);
int can_player_play(game_t *game, unsigned int player_index);
int can_figure_move(game_t *game, unsigned int figure_index, unsigned int *d_index);
void move_figure(client_t *client, unsigned int figure_index);
int find_home(int figure_index);
int all_players_finished(game_t *game);
int has_all_figures_at_home(game_t *game, int player_index);
void broadcast_game_finish(game_t *game, client_t *skip);
void clear_all_games();

#endif	/* GAME_H */
