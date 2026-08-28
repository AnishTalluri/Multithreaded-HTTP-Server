#include "queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

struct queue {
    void **buffer;
    int in;
    int out;
    int max;
    int curr;
    pthread_mutex_t lock;
    pthread_cond_t empty;
    pthread_cond_t full;
};

queue_t *queue_new(int size) {
    queue_t *q = malloc(sizeof(queue_t));
    q->buffer = (void **) malloc(size * sizeof(void *));
    q->in = 0;
    q->out = 0;
    q->max = size;
    q->curr = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->empty, NULL);
    pthread_cond_init(&q->full, NULL);
    return q;
}

void queue_delete(queue_t **q) {
    if ((q == NULL) || (*q == NULL)) {
        return;
    }

    pthread_mutex_destroy(&(*q)->lock);
    pthread_cond_destroy(&(*q)->empty);
    pthread_cond_destroy(&(*q)->full);

    free((*q)->buffer);
    free(*q);
    *q = NULL;
}

bool queue_push(queue_t *q, void *elem) {
    if (q == NULL) {
        return false;
    }

    pthread_mutex_lock(&q->lock);
    while (q->curr == q->max) {
        pthread_cond_wait(&q->full, &q->lock);
    }

    q->buffer[q->in] = elem;
    q->in = (q->in + 1) % q->max;
    q->curr++;

    pthread_cond_signal(&q->empty);
    pthread_mutex_unlock(&q->lock);

    return true;
}

bool queue_pop(queue_t *q, void **elem) {
    if (q == NULL) {
        return false;
    }

    pthread_mutex_lock(&q->lock);
    while (q->curr == 0) {
        pthread_cond_wait(&q->empty, &q->lock);
    }

    *elem = q->buffer[q->out];
    q->out = (q->out + 1) % q->max;
    q->curr--;

    pthread_cond_signal(&q->full);
    pthread_mutex_unlock(&q->lock);

    return true;
}
