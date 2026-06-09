#include "hashtable.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// Node structure for collision handling
typedef struct HashNode {
    char *key;
    rwlock_t *lock;
    struct HashNode *next;
} HashNode;

struct HashTable {
    size_t size;
    HashNode **buckets;
    pthread_mutex_t mutex; // Mutex for thread-safe access
};

unsigned int hash(const char *key, size_t size) {
    unsigned long hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % size;
}

HashTable *hash_table_new(size_t size) {
    HashTable *table = malloc(sizeof(HashTable));
    table->size = size;
    table->buckets = calloc(size, sizeof(HashNode *));
    pthread_mutex_init(&table->mutex, NULL); // Initialize the mutex
    return table;
}

rwlock_t *hash_table_get_or_create(HashTable *table, const char *key) {
    unsigned int index = hash(key, table->size);

    pthread_mutex_lock(&table->mutex); // Lock the table for thread-safe access
    HashNode *node = table->buckets[index];

    // Search for the key in the bucket
    while (node) {
        if (strcmp(node->key, key) == 0) {
            pthread_mutex_unlock(&table->mutex); // Unlock before returning
            return node->lock;
        }
        node = node->next;
    }

    // Key not found, create a new node
    node = malloc(sizeof(HashNode));
    node->key = strdup(key);
    node->lock = rwlock_new(WRITERS, 0); // Create a new rwlock
    node->next = table->buckets[index];
    table->buckets[index] = node;

    pthread_mutex_unlock(&table->mutex); // Unlock after modifying the table
    return node->lock;
}

void hash_table_free(HashTable *table) {
    pthread_mutex_lock(&table->mutex); // Lock the table during free
    for (size_t i = 0; i < table->size; i++) {
        HashNode *node = table->buckets[i];
        while (node) {
            HashNode *temp = node;
            free(temp->key);
            rwlock_delete(&temp->lock);
            node = node->next;
            free(temp);
        }
    }
    free(table->buckets);
    pthread_mutex_unlock(&table->mutex);
    pthread_mutex_destroy(&table->mutex); // Destroy the mutex
    free(table);
}
