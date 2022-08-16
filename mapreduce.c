#include "mapreduce.h"

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>  // add semaphore
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hashmap.h"

typedef struct __kv {
    char* key;
    char* value;
    struct __kv* next;
} kv;

// linkedlist
// TODO: optimize this linkedlist
typedef struct __kv_list {
    int partition_number;
    kv* head;
    kv* current;
} kv_list;

sem_t mapper_sem;
int partitions_number;
kv_list** lists;
Mapper mapper;
Reducer reducer;
Partitioner partitioner;

// initialize the linkedlist
void init_kv_lists(int num_partitions) {
    lists = (kv_list**)malloc(num_partitions * sizeof(kv_list*));
    if (lists == NULL) {
        printf("Malloc error! %s\n", strerror(errno));
        exit(1);
    }

    for (int i = 0; i < num_partitions; i++) {
        lists[i] = (kv_list*)malloc(sizeof(kv_list));
        if (lists[i] == NULL) {
            printf("Malloc error! %s\n", strerror(errno));
            exit(1);
        }
        lists[i]->head = NULL;
        lists[i]->current = NULL;
        lists[i]->partition_number = i;
    }
}

// add a key-value pair to the linkedlist
void add_to_list(kv_list* list, char* key, char* value) {
    // create a new kv
    kv* new_kv = (kv*)malloc(sizeof(kv));
    if (new_kv == NULL) {
        printf("Malloc error! %s\n", strerror(errno));
        exit(1);
    }
    new_kv->next = NULL;
    new_kv->key = strdup(key);
    new_kv->value = strdup(value);

    do {
        new_kv->next = list->head;
    } while (!__sync_bool_compare_and_swap(&list->head, new_kv->next, new_kv));
}

// get the next key-value pair from the linkedlist
char* get_func(char* key, int partition_number) {
    kv_list* list = lists[partition_number];
    kv* current = list->current;

    if (current != NULL && strcmp(current->key, key) == 0) {
        char* currentval = current->value;
        list->current = list->current->next;
        current = current->next;
        return currentval;
    }

    return NULL;
}

// compare the key-value pair with the another key-value pair
int cmp(const void* a, const void* b) {
    kv* kv_a = *(kv**)a;
    kv* kv_b = *(kv**)b;
    return strcmp(kv_a->key, kv_b->key);
}

// emit intermediate key-value pair to the linkedlist
void MR_Emit(char* key, char* value) {
    int partition_num = (*partitioner)(key, partitions_number);
    add_to_list(lists[partition_num], key, value);
    return;
}

// merge sort the linkedlist
// TODO: optimize this merge sort
void sort(kv** head) {
    int size = 0;
    kv* curr = *head;

    while (curr != NULL) {
        curr = curr->next;
        size++;
    }
    if (size == 0 || size == 1) {
        return;
    }

    // convert list to array of pointers
    kv** arr = (kv**)malloc(sizeof(kv*) * (size + 1));
    kv** temp = arr;
    curr = *head;
    while (curr != NULL) {
        *temp = curr;
        temp++;
        curr = curr->next;
    }
    *temp = NULL;

    // sort the array
    qsort(arr, size, sizeof(kv**), cmp);

    for (int i = 0; i < size; ++i) {
        arr[i]->next = arr[i + 1];
    }

    *head = *arr;
    free(arr);
}

// copy from p4 desecription
unsigned long MR_DefaultHashPartition(char* key, int num_partitions) {
    unsigned long hash = 5381;
    int c;
    while ((c = *key++) != '\0') hash = hash * 33 + c;
    return hash % num_partitions;
}

// map helper function
void* map_helper(void* arg) {
    char* filename = (char*)arg;

    sem_wait(&mapper_sem);
    (*mapper)(filename);
    sem_post(&mapper_sem);

    pthread_exit(NULL);
}

// reduce helper function
void* reduce_helper(void* arg) {
    int partition_num = *(int*)arg;

    kv_list* list = lists[partition_num];
    list->current = list->head;
    kv* kv = list->head;

    while (kv != NULL) {
        (*reducer)(kv->key, get_func, partition_num);
        kv = list->current;
    }

    free(arg);
    pthread_exit(NULL);
}

// sort helper function
// TODO: optimize this sort
void* sort_helper(void* arg) {
    int partition_num = *(int*)arg;

    kv_list* list = lists[partition_num];
    sort(&list->head);

    free(arg);
    pthread_exit(NULL);
}

// free the linkedlist
void free_lists(int num_lists) {
    for (int i = 0; i < num_lists; i++) {
        kv* head = lists[i]->head;
        while (head != NULL) {
            kv* next = head->next;
            free(head->key);
            free(head->value);
            free(head);
            head = next;
        }
        free(lists[i]);
    }

    free(lists);
}

// mapreduce
void MR_Run(int argc, char* argv[], Mapper map, int num_mappers, Reducer reduce,
            int num_reducers, Partitioner partition) {
    mapper = map;
    reducer = reduce;
    partitioner = partition == NULL ? MR_DefaultHashPartition : partition;

    // init semaphores
    sem_init(&mapper_sem, 0, num_mappers);

    // create lists
    partitions_number = num_reducers;
    init_kv_lists(num_reducers);

    // create threads
    pthread_t* mapper_threads =
        (pthread_t*)malloc(sizeof(pthread_t) * (argc - 1));
    if (mapper_threads == NULL) {
        printf("Malloc error! %s\n", strerror(errno));
        exit(1);
    }

    for (int i = 1; i < argc; i++) {
        pthread_create(&mapper_threads[i - 1], NULL, &map_helper,
                       (void*)argv[i]);
    }

    // wait for mappers to finish
    for (int i = 0; i < argc - 1; i++) {
        pthread_join(mapper_threads[i], NULL);
    }
    free(mapper_threads);

    // sort the lists
    pthread_t* sorter_threads =
        (pthread_t*)malloc(sizeof(pthread_t) * num_reducers);
    if (sorter_threads == NULL) {
        printf("Malloc error! %s\n", strerror(errno));
        exit(1);
    }

    for (int i = 0; i < num_reducers; i++) {
        int* arg = (int*)malloc(sizeof(int));
        *arg = i;

        pthread_create(&sorter_threads[i], NULL, &sort_helper, (void*)arg);
    }

    // wait for sorters to finish
    for (int i = 0; i < num_reducers; i++) {
        pthread_join(sorter_threads[i], NULL);
    }
    free(sorter_threads);

    // create threads for reducers
    pthread_t* reducers_threads =
        (pthread_t*)malloc(sizeof(pthread_t) * num_reducers);
    if (reducers_threads == NULL) {
        printf("Malloc error! %s\n", strerror(errno));
        exit(1);
    }

    for (int i = 0; i < num_reducers; i++) {
        int* arg = (int*)malloc(sizeof(int));
        *arg = i;

        pthread_create(&reducers_threads[i], NULL, &reduce_helper, (void*)arg);
    }

    // wait for reducers to finish
    for (int i = 0; i < num_reducers; i++) {
        pthread_join(reducers_threads[i], NULL);
    }
    free(reducers_threads);

    sem_destroy(&mapper_sem);
    free_lists(num_reducers);
}