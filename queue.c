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
 * File: queue.c
 * Description: Queue implementation for outgoing packets
 * 
 * -----------------------------------------------------------------------------
 * 
 * @author: Martin Kucera, 2014
 * @version: 1.02
 * 
 */

/*****
** Queue.c
** - implements the methods declared in Queue.h
** Notes
** - this package is provided as is with no warranty.
** - the author is not responsible for any damage caused
**   either directly or indirectly by using this package.
** - anybody is free to do whatever he/she wants with this
**   package as long as this header section is preserved.
** Created on 2004-01-20 by
** - Roger Zhang (rogerz@cs.dal.ca)
** Modifications
** -
** Last compiled under Linux with gcc-3
*/

#include <stdlib.h>
#include "queue.h"

void queue_init(Queue *q)
{
    q->size = 0;
    q->head = q->tail = NULL;
}

int queue_size(Queue *q)
{
    return q->size;
}

void queue_push(Queue *q, void *element)
{
    if (!q->head) {
        q->head = (QueueNode*)malloc(sizeof(QueueNode));
        q->head->data = element;
        q->tail = q->head;
    } else {
        q->tail->link = (QueueNode*)malloc(sizeof(QueueNode));
        q->tail = q->tail->link;
        q->tail->data = element;
    }

    q->tail->link = NULL;
    q->size++;
}

void *queue_front(Queue *q)
{
    return q->size ? q->head->data : NULL;
}

void queue_pop(Queue *q, int release)
{
    if (q->size) {
        QueueNode *temp = q->head;
        if (--(q->size)) {
            q->head = q->head->link;
        } else {
            q->head = q->tail = NULL;
        }
        /* Release memory accordingly */
        if (release) {
            free(temp->data);
        }
        free(temp);
    }
}

void queue_clear(Queue *q, int release)
{
    while (q->size) {
        QueueNode *temp = q->head;
        q->head = q->head->link;
        if (release) {
            free(temp->data);
        }
        free(temp);
        q->size--;
    }

    q->head = q->tail = NULL;
}

