/**
 * @file   test_hash.c
 * @brief  Unit tests for the generic hash interface.
 * @author Mikey Austin
 * @date   2014
 */

#include "test.h"
#include "../hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define HASH_SIZE 2

#define TEST_KEY1 "test key 1"
#define TEST_KEY2 "test key 2"
#define TEST_KEY3 "test key 3"
#define TEST_KEY4 "test key 4"
#define TEST_KEY5 "test key 5"

#define TEST_VAL1 "test value 1"
#define TEST_VAL2 "test value 2"
#define TEST_VAL3 "test value 3"
#define TEST_VAL4 "test value 4"
#define TEST_VAL5 "test value 5"

/* Include the private hash callback functions. */
int  lookup(const char *key);
void destroy(struct Hash_entry *entry);

int
main()
{
    Hash_T hash;
    char *s, *s1, *s2, *s3, *s4, *s5;

    /* Test hash creation. */
    hash = Hash_create(HASH_SIZE, lookup, destroy);

    TEST_START(13);

    /* Create a hash string keys to string values. */
    TEST_OK((hash->size == HASH_SIZE), "Hash size is set correctly");
    TEST_OK((hash->num_entries == 0), "Hash number of entries is zero as expected");

    /* Test insertion of keys in different orders. */
    s1 = (char *) malloc(strlen(TEST_VAL1) + 1); strcpy(s1, TEST_VAL1);
    s2 = (char *) malloc(strlen(TEST_VAL2) + 2); strcpy(s2, TEST_VAL2);
    s3 = (char *) malloc(strlen(TEST_VAL3) + 3); strcpy(s3, TEST_VAL3);
    s4 = (char *) malloc(strlen(TEST_VAL4) + 4); strcpy(s4, TEST_VAL4);
    s5 = (char *) malloc(strlen(TEST_VAL5) + 5); strcpy(s5, TEST_VAL5);

    Hash_insert(hash, TEST_KEY5, (void *) s5);
    Hash_insert(hash, TEST_KEY4, (void *) s4);
    Hash_insert(hash, TEST_KEY3, (void *) s3);

    /* Test that the realloc worked. */
    TEST_OK((hash->num_entries == 3), "Hash number of entries is as expected");
    TEST_OK((hash->size == (2 * HASH_SIZE)), "Hash size is as expected");
    
    Hash_insert(hash, TEST_KEY2, (void *) s2);
    Hash_insert(hash, TEST_KEY1, (void *) s1);

    TEST_OK((hash->num_entries == 5), "Hash size is as expected");
    TEST_OK((hash->size == (2 * 2 * HASH_SIZE)), "Hash size is as expected");

    s = (char *) Hash_get(hash, TEST_KEY1);
    TEST_OK((strcmp(s, TEST_VAL1) == 0), "Hash entry 1 inserted correctly");

    s = (char *) Hash_get(hash, TEST_KEY2);
    TEST_OK((strcmp(s, TEST_VAL2) == 0), "Hash entry 2 inserted correctly");

    s = (char *) Hash_get(hash, TEST_KEY3);
    TEST_OK((strcmp(s, TEST_VAL3) == 0), "Hash entry 3 inserted correctly");

    s = (char *) Hash_get(hash, TEST_KEY4);
    TEST_OK((strcmp(s, TEST_VAL4) == 0), "Hash entry 4 inserted correctly");

    s = (char *) Hash_get(hash, TEST_KEY5);
    TEST_OK((strcmp(s, TEST_VAL5) == 0), "Hash entry 5 inserted correctly");

    /* Reset the hash and verify that the last value is no longer present. */
    Hash_reset(hash);
    s = (char *) Hash_get(hash, TEST_KEY5);
    TEST_OK((s == NULL), "Hash reset correctly");
    TEST_OK((hash->num_entries == 0), "Hash number of entries is zero as expected");

    /* Destroy the hash. */
    Hash_destroy(hash);

    TEST_COMPLETE;
}

int
lookup(const char *key)
{
    return strlen(key);
}

void
destroy(struct Hash_entry *entry)
{
    /* Free the malloced value. */
    if(entry && entry->v) {
        free(entry->v);
    }
}
