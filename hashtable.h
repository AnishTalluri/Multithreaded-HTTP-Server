#pragma once

#include "rwlock.h"
#include <stddef.h>

typedef struct HashTable HashTable;

HashTable *hash_table_new(size_t size); // Create a new hash table
rwlock_t *hash_table_get_or_create(HashTable *table, const char *key); // Fetch or create lock
void hash_table_free(HashTable *table); // Free all locks and the table
