/*
    Code referenced based on Tutor Raj's pseudocode provided during his office hours.
*/

#include "rwlock.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

struct rwlock {
    pthread_mutex_t lock;
    pthread_cond_t read;
    pthread_cond_t write;

    int active_readers;
    int waiting_readers;

    int active_writers;
    int waiting_writers;

    int consective_reads;
    int n; // n-way priority

    PRIORITY p;
};

rwlock_t *rwlock_new(PRIORITY p, uint32_t n) {
    rwlock_t *rw = malloc(sizeof(rwlock_t));
    pthread_mutex_init(&rw->lock, NULL);
    pthread_cond_init(&rw->read, NULL);
    pthread_cond_init(&rw->write, NULL);

    rw->p = p;

    if (rw == NULL) {
        return NULL;
    }

    rw->active_readers = 0;
    rw->waiting_readers = 0;

    rw->active_writers = 0;
    rw->waiting_writers = 0;

    rw->consective_reads = 0;
    rw->n = n;

    return rw;
}

void rwlock_delete(rwlock_t **rw) {
    if ((rw == NULL) || (*rw == NULL)) {
        return;
    }

    pthread_mutex_destroy(&(*rw)->lock);
    pthread_cond_destroy(&(*rw)->read);
    pthread_cond_destroy(&(*rw)->write);

    free(*rw);
}

void reader_lock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    rw->waiting_readers++;

    if (rw->p == WRITERS) {
        while ((rw->active_writers > 0) || (rw->waiting_writers > 0)) {
            pthread_cond_wait(&rw->read, &rw->lock);
        }
    }

    else if (rw->p == READERS) {
        while (rw->active_writers > 0) {
            pthread_cond_wait(&rw->read, &rw->lock);
        }
    }

    else if (rw->p == N_WAY) {
        while ((rw->active_writers > 0)
               || (rw->waiting_writers > 0 && rw->consective_reads >= rw->n)) {
            pthread_cond_wait(&rw->read, &rw->lock);
        }
    }

    rw->waiting_readers--;
    rw->active_readers++;
    rw->consective_reads++;
    pthread_mutex_unlock(&rw->lock);
}

void reader_unlock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    rw->active_readers--;

    if (rw->p == WRITERS) {
        if (rw->active_writers == 0) {
            pthread_cond_signal(&rw->write);
        }
    }

    else if (rw->p == READERS) {
        if (rw->active_readers == 0 || rw->waiting_readers) {
            pthread_cond_signal(&rw->write);
        }
    }

    else if (rw->p == N_WAY) {
        if (rw->active_readers == 0 || rw->active_readers >= rw->consective_reads) {
            pthread_cond_signal(&rw->write);
        }
    }

    pthread_mutex_unlock(&rw->lock);
}

void writer_lock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    rw->waiting_writers++;

    if (rw->p == WRITERS) {
        while ((rw->active_readers > 0) || (rw->active_writers > 0)) {
            pthread_cond_wait(&rw->write, &rw->lock);
        }
    }

    else if (rw->p == READERS) {
        while ((rw->active_readers > 0) || (rw->active_writers > 0) || (rw->waiting_readers > 0)) {
            pthread_cond_wait(&rw->write, &rw->lock);
        }
    }

    else if (rw->p == N_WAY) {
        while (rw->active_readers > 0 || rw->active_writers > 0
               || (rw->consective_reads < rw->n && rw->waiting_readers > 0)) {
            pthread_cond_wait(&rw->write, &rw->lock);
        }
    }

    rw->consective_reads = 0;
    rw->waiting_writers--;
    rw->active_writers++;
    pthread_mutex_unlock(&rw->lock);
}

void writer_unlock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    rw->active_writers--;

    if (rw->p == WRITERS) {
        if (rw->waiting_writers > 0) {
            pthread_cond_signal(&rw->write);
        }

        else {
            pthread_cond_broadcast(&rw->read);
        }
    }

    else if (rw->p == READERS || rw->p == N_WAY) {
        if (rw->waiting_readers > 0) {
            pthread_cond_broadcast(&rw->read);
        }

        else if (rw->waiting_writers > 0) {
            pthread_cond_signal(&rw->write);
        }
    }

    pthread_mutex_unlock(&rw->lock);
}
