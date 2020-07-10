#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include "myalloc.h"

#define foreach(item, array) \
    for(int keep = 1, \
            count = 0,\
            size = sizeof (array) / sizeof *(array); \
        keep && count != size; \
        keep = !keep, count++) \
      for(item = (array) + count; keep; keep = !keep)

#define loop(i, start, end) \
  for(i=start; i < end; i++)

struct workload {
    int *free_order;      // An array with num_of_allocs indexes. Workload should be freed in this order.
    int *coalesce_order;  // An array with num_of_allocs 1s or 0s. If 1 coalesce will be called after the free.
    uint num_of_allocs;   // Number of allocs.
    size_t block_size;      // Size of each alloc.
};

// Taken from http://benpfaff.org/writings/clc/shuffle.html.
// Found through: https://stackoverflow.com/questions/6127503/shuffle-array-in-c
/* Arrange the N elements of ARRAY in random order.
   Only effective if N is much smaller than RAND_MAX;
   if this may not be the case, use a better random
   number generator. */
void shuffle(int *array, size_t n) {
    if (n > 1) {
        size_t i;
        for (i = 0; i < n - 1; i++) {
            size_t j = i + rand() / (RAND_MAX / (n - i) + 1);
            int t = array[j];
            array[j] = array[i];
            array[i] = t;
        }
    }
}


/* From: https://www.guyrutenberg.com/2007/09/22/profiling-code-using-clock_gettime/ 
 * Modified to convert timespec to float
 * */
double diff2float(struct timespec *start, struct timespec *end) {
    double f;

    struct timespec temp;

    if ((end->tv_nsec - start->tv_nsec) < 0) {
        temp.tv_sec = end->tv_sec - start->tv_sec - 1;
        temp.tv_nsec = 1000000000 + end->tv_nsec - start->tv_nsec;
    } else {
        temp.tv_sec = end->tv_sec - start->tv_sec;
        temp.tv_nsec = end->tv_nsec - start->tv_nsec;
    }

    f = temp.tv_sec + (temp.tv_nsec / 1000000000.0);

    return f;
}


void *alloc_check_2(size_t size, size_t realsize) {
    /* Like alloc_check except you can give a realsize which
     * will put exactly realsize number of bytes into memory
     * starting at ptr.
     */
    void *ptr; /* temp ptr for allocation */

    if ((ptr = myalloc(size)) == NULL) {
        printf("!!! Allocation of size %u failed!\n", (unsigned) size);
        return NULL;
    } else {
        // Fill ptr with data
        memset((char *) ptr, rand(), realsize);
        return ptr;
    }
}

void *alloc_check(size_t size) {
    /* this is helper function that verifies whether the
     * allocation succeeded or failed and prints out some useful
     * information
     */

    return alloc_check_2(size, size);
}

// If good returns 1, else 0.
int is_header_good(void *ptr, int size) {
    if (ptr == NULL) {
        return 0;
    }
    header_t *t = (header_t *) (ptr - sizeof(header_t));
    if (t->magic != HEAPMAGIC) {
        printf("Header Magic number not correct.\n");
        // Magic is not right.
        return 0;
    }
    if (t->size < size) {
        printf("Header says size is too small.\n");
        // Size is too small.
        return 0;
    }
    return 1;
}

// If good returns 1, else 0.
int is_data_good(char *ptr, int size) {
    if (size < 1) return 1;
    int i = size;
    while (--i > 0 && ptr[i] == ptr[0]);
    return i == 0;
}

// If good returns 1, else 0
int is_free_list_good(node_t *head, int *correct, int size) {
    int cur_size;
    int left[size];
    memcpy(&left, correct, size * sizeof(int));
    node_t *cur = head;
    if (head == NULL) {
        printf("Freelist check: Bad head\n");
        return 0;
    }
    while (cur != NULL) {
        cur_size = cur->size;
        int found = 0;
        for (int i = 0; i < size; i++) {
            if (left[i] == cur_size) {
                left[i] = -1;
                found = 1;
                break;
            }
        }
        if (found == 0) {
            printf("Freelist check: Freelist node contains incorrect size.\n");
            printf("Freelist check: size is %d\n", cur_size);
            return 0;
        }
        cur = cur->next;
    }
    for (int i = 0; i < size; i++) {
        if (left[i] != -1) {
            printf("Freelist check: Freelist did not contain enough nodes.\n");
            return 0;
        }
    }
    return 1;
}

// If good returns 1, else 0.
int complete_state_check(node_t *head, int *freelist, int freelist_size, void **ptr, int ptr_size, int *sizes) {
    if (!is_free_list_good(head, freelist, freelist_size)) {
        printf("Free list is not correct!\nFreelist trace:");
        print_freelist_from(__head);
        printf("However it should have the following sizes\n[");
        if (freelist_size > 0)
            printf("%d", freelist[0]);
        for (int i = 1; i < freelist_size; i++) {
            printf(", %d", freelist[i]);
        }
        printf("]\n");
        return 0;
    }
    for (int i = 0; i < ptr_size; i++) {
        if (sizes[i] == -2) {
            // Good
        } else if (ptr[i] == NULL && sizes[i] == -1) {
            // Good
        } else if (ptr[i] == NULL) {
            printf("You program failed to alloc ptr[%d]\n", i);
        } else if (!is_header_good(ptr[i], sizes[i])) {
            printf("Found Incorrect Header @ ptr[%d]\n", i);
            return 0;
        } else if (!is_data_good(ptr[i], sizes[i])) {
            printf("Data Corrupted in ptr[%d]\n", i);
            return 0;
        }
    }
    return 1;
}

void *thread_main(void *args) {
    int numblocks = 256;
    void *ptr[numblocks];
    size_t block_size = 16;
    int *good = (int *) malloc(sizeof(int));
    *good = 1;

    for (int i = 0; i < numblocks; i++) {
        ptr[i] = alloc_check(block_size);
        if (ptr[i] == NULL) {
            *good = 0;
            return (void *) good;
        }
    }

    for (int i = 0; i < numblocks; i++) {
        myfree(ptr[i]);
    }

    return (void *) good;
}

void *thread_main2(void *args) {
    struct workload *load = (struct workload *) args;
    int *good = (int *) malloc(sizeof(int));
    void *ptr[load->num_of_allocs];

    *good = 1;

    for (int i = 0; i < load->num_of_allocs; i++) {
        ptr[i] = alloc_check(load->block_size);
        if (ptr[i] == NULL) {
            *good = 0;
            return (void *) good;
        }
    }

    for (int i = 0; i < load->num_of_allocs; i++) {
        if (!is_data_good((char *) ptr[i], load->block_size)) {
            *good = 0;
            return (void *) good;
        }
    }

    for (int i = 0; i < load->num_of_allocs; i++) {
        myfree(ptr[load->free_order[i]]);
        if (load->coalesce_order[i]) {
            coalesce_freelist();
        }
    }

    return (void *) good;
}

int main(int argc, char *argv[]) {
    int start_test = 1;
    int end_test = 12;
    int cur_test;
    // Parse args
    if (argc > 2) {
        start_test = atoi(argv[1]);
        end_test = atoi(argv[2]);
    } else if (argc > 1) {
        start_test = atoi(argv[1]);
        end_test = start_test;
    }

    // Set up random number generator.
    int seed = time(NULL); // get current time for seed
    srand(seed);

    void *ptr[1024];
    int freelist[1024];
    int sizes[1024];
    int numthreads = 4;
    int nodesize = sizeof(node_t);
    int headersize = sizeof(header_t);

    // Alloc threads array.
    pthread_t *threads = NULL;
    threads = (pthread_t *) malloc(sizeof(pthread_t) * numthreads);
    struct workload load;
    int i;

    /* let's print some sizes of types so we know what they are */
    printf("sizes:\n");
    printf("void *:\t\t%lu\n", sizeof(void *));
    printf("long unsigned:\t%lu\n", sizeof(long unsigned));
    printf("node_t:\t\t%lu\n", sizeof(node_t));
    printf("header_t:\t%lu\n", sizeof(header_t));
    printf("numthreads:\t%d\n", numthreads);
    printf("heap size:\t%d\n", HEAPSIZE);

    struct timespec start, end;
    clock_gettime(CLOCK_REALTIME, &start);

    loop(cur_test, start_test, end_test+1) {
        switch (cur_test) {
            case 1:
                // TEST 1: Test simple alloc
                ptr[0] = alloc_check(1024);
                ptr[1] = alloc_check(1024);
                ptr[2] = alloc_check(512);
                sizes[0] = 1024;
                sizes[1] = 1024;
                sizes[2] = 512;

                freelist[0] = HEAPSIZE - 1024 - 1024 - 512 - (nodesize * 1) - (headersize * 3);
                if (complete_state_check(__head, freelist, 1, ptr, 3, sizes)) {
                    printf("Test 1: Simple Allocation is Good :)\n");
                } else {
                    printf("Test 1: Failed, Allocation is not working :(\n");
                    return 1;
                }
                break;

            case 2:
                // TEST 2: Test advanced alloc
                ptr[0] = alloc_check(1024);
                ptr[1] = alloc_check(1024);
                ptr[3] = alloc_check(512);
                ptr[2] = alloc_check(HEAPSIZE - 2560);
                sizes[0] = 1024;
                sizes[1] = 1024;
                sizes[2] = -1;   // NULL
                sizes[3] = 512;

                freelist[0] = HEAPSIZE - 1024 - 1024 - 512 - (nodesize * 1) - (headersize * 3);
                if (complete_state_check(__head, freelist, 1, ptr, 4, sizes)) {
                    printf("Test 2: Advanced Allocation is Good :)\n");
                } else {
                    printf("Test 2: Failed, Allocation is not working :(\n");
                    return 1;
                }
                break;

            case 3:
                // TEST 3: Test alloc and free
                ptr[0] = alloc_check(1024);
                ptr[1] = alloc_check(1024);
                ptr[2] = alloc_check(1024);
                myfree(ptr[1]);
                sizes[0] = 1024;
                sizes[1] = -2;   // Don't check, not allocated.
                sizes[2] = 1024;

                freelist[0] = HEAPSIZE - 1024 - 1024 - 1024 - (nodesize * 1) - (headersize * 3);
                freelist[1] = 1024;
                if (complete_state_check(__head, freelist, 2, ptr, 3, sizes)) {
                    printf("Test 3: Simple Free is Good :)\n");
                } else {
                    printf("Test 3: Failed, Free is not working :(\n");
                    return 1;
                }
                break;

            case 4:
                // TEST 4: Test double free
                ptr[0] = alloc_check(1024);
                ptr[1] = alloc_check(1024);
                ptr[2] = alloc_check(1024);
                myfree(ptr[1]);
                myfree(ptr[1]);
                sizes[0] = 1024;
                sizes[1] = -2;   // Don't check, not allocated.
                sizes[2] = 1024;

                freelist[0] = HEAPSIZE - 1024 - 1024 - 1024 - (nodesize * 1) - (headersize * 3);
                freelist[1] = 1024;
                if (complete_state_check(__head, freelist, 2, ptr, 3, sizes)) {
                    printf("Test 4: If you saw the message '...The heap is corrupt!' above then,\n");
                    printf("Test 4: Double Free is Good :)\n");
                } else {
                    printf("Test 4: Failed, Double Free is not working :(\n");
                    return 1;
                }
                break;

            case 5:
                // TEST 5: Test alloc, free, coalesce, alloc
                ptr[0] = alloc_check(512);
                ptr[1] = alloc_check(512);
                ptr[2] = alloc_check(512);
                ptr[3] = alloc_check(2048);
                myfree(ptr[1]);
                myfree(ptr[2]);
                sizes[0] = 512;
                sizes[1] = -2;   // Don't check, not allocated.
                sizes[2] = -2;   // Don't check, not allocated.
                sizes[3] = 2048;

                freelist[0] = HEAPSIZE - (512 * 3) - 2048 - (nodesize * 1) - (headersize * 4);
                freelist[1] = 512;
                freelist[2] = 512;
                if (complete_state_check(__head, freelist, 3, ptr, 4, sizes)) {
                    printf("Test 5.1: Coalesce is Good so far :)\n");
                } else {
                    printf("Test 5.1: Failed, Coalesce is not working :(\n");
                    return 1;
                }

                coalesce_freelist();

                freelist[0] = HEAPSIZE - (512 * 3) - 2048 - (nodesize * 1) - (headersize * 4);
                freelist[1] = 1024 + headersize;
                if (complete_state_check(__head, freelist, 2, ptr, 4, sizes)) {
                    printf("Test 5.2: Coalesce is Good so far :)\n");
                } else {
                    printf("Test 5.2: Failed, Coalesce is not working :(\n");
                    return 1;
                }

                ptr[4] = alloc_check(1000);

                freelist[0] = HEAPSIZE - (512 * 3) - 2048 - (nodesize * 1) - (headersize * 4);
                freelist[1] = 24;
                if (complete_state_check(__head, freelist, 2, ptr, 4, sizes)) {
                    printf("Test 5.3: Coalesce is Good :)\n");
                } else {
                    printf("Test 5.3: Failed, Coalesce is not working :(\n");
                    return 1;
                }
                break;

            case 6:
                // TEST 6: Test coalesce and have node of size 0.
                ptr[0] = alloc_check(512);
                ptr[1] = alloc_check(512);
                ptr[2] = alloc_check(512);
                ptr[3] = alloc_check(2048);
                myfree(ptr[1]);
                myfree(ptr[2]);
                sizes[0] = 512;
                sizes[1] = -2;   // Don't check, not allocated.
                sizes[2] = -2;   // Don't check, not allocated.
                sizes[3] = 2048;

                freelist[0] = HEAPSIZE - (512 * 3) - 2048 - (nodesize * 1) - (headersize * 4);
                freelist[1] = 512;
                freelist[2] = 512;
                if (complete_state_check(__head, freelist, 3, ptr, 4, sizes)) {
                    printf("Test 6.1: Advanced Coalesce is Good so far :)\n");
                } else {
                    printf("Test 6.1: Failed, Coalesce is not working :(\n");
                    return 1;
                }

                coalesce_freelist();

                freelist[0] = HEAPSIZE - (512 * 3) - 2048 - (nodesize * 1) - (headersize * 4);
                freelist[1] = 1024 + headersize;
                if (complete_state_check(__head, freelist, 2, ptr, 4, sizes)) {
                    printf("Test 6.2: Advanced Coalesce is Good so far :)\n");
                } else {
                    printf("Test 6.2: Failed, Coalesce is not working :(\n");
                    return 1;
                }

                ptr[4] = alloc_check(1024);

                freelist[0] = HEAPSIZE - (512 * 3) - 2048 - (nodesize * 1) - (headersize * 4);
                freelist[1] = 0;
                if (complete_state_check(__head, freelist, 2, ptr, 4, sizes)) {
                    printf("Test 6.3: Advanced Coalesce is Good :)\n");
                } else {
                    printf("Test 6.3: Failed, Coalesce is not working :(\n");
                    return 1;
                }
                break;

            case 7:
                // TEST 7: Test for memory leak.
                ptr[0] = alloc_check(512);
                ptr[1] = alloc_check(16);
                ptr[2] = alloc_check(512);
                sizes[0] = 512;
                sizes[1] = 16;
                sizes[2] = 512;

                freelist[0] = HEAPSIZE - (512 * 2) - 16 - (nodesize * 1) - (headersize * 3);
                if (complete_state_check(__head, freelist, 1, ptr, 3, sizes)) {
                    printf("Test 7.1: Memory Leak is Good so far :)\n");
                } else {
                    printf("Test 7.1: You have a memory leak! This is bad :(\n");
                    return 1;
                }
                myfree(ptr[1]);
                sizes[1] = -2;
                freelist[0] = HEAPSIZE - (512 * 2) - 16 - (nodesize * 1) - (headersize * 3);
                freelist[1] = 16;
                if (complete_state_check(__head, freelist, 2, ptr, 3, sizes)) {
                    printf("Test 7.2: Memory Leak is Good so far :)\n");
                } else {
                    printf("Test 7.2: You have a memory leak! This is bad :(\n");
                    return 1;
                }
                ptr[1] = alloc_check_2(14, 16);
                sizes[1] = 16;
                freelist[0] = HEAPSIZE - (512 * 2) - 16 - (nodesize * 1) - (headersize * 3);
                if (complete_state_check(__head, freelist, 1, ptr, 3, sizes)) {
                    printf("Test 7.3: Passed. I could not find a Memory Leak :)\n");
                } else {
                    printf("Test 7.3: You have a memory leak! Or you are not using memory optimally. This is bad :(\n");
                    return 1;
                }
                break;

            case 8:
                // TEST 8: Test for sorting in the coalesce.
                loop(i, 0, 10) {
                    ptr[i] = alloc_check(32);
                    sizes[i] = 32;
                }

                freelist[0] = HEAPSIZE - (32 * 10) - (nodesize * 1) - (headersize * 10);
                if (!complete_state_check(__head, freelist, 1, ptr, 10, sizes)) {
                    printf("Test 8.1 :(\n");
                    return 1;
                }

                int nodes1[] = {2, 6, 8, 4};
                foreach(int *j, nodes1) myfree(ptr[*j]);
                foreach(int *j, nodes1) sizes[*j] = -2;
                freelist[0] = HEAPSIZE - (32 * 10) - (nodesize * 1) - (headersize * 10);
                loop(i, 1, 5) freelist[i] = 32;
                if (!complete_state_check(__head, freelist, 5, ptr, 10, sizes)) {
                    printf("Test 8.2 :(\n");
                    return 1;
                }

                int nodes2[] = {1,5,7,0,9,3};
                foreach(int *j, nodes2) myfree(ptr[*j]);
                foreach(int *j, nodes2) sizes[*j] = -2;
                freelist[0] = HEAPSIZE - (32 * 10) - (nodesize * 1) - (headersize * 10);
                loop(i, 1, 11) freelist[i] = 32;
                if (!complete_state_check(__head, freelist, 11, ptr, 10, sizes)) {
                    printf("Test 8.3 :(\n");
                    return 1;
                }

                coalesce_freelist();
                freelist[0] = HEAPSIZE - sizeof(node_t);
                if (!complete_state_check(__head, freelist, 1, ptr, 3, sizes)) {
                    printf("Test 8.4: Coalesce is broken :(\n");
                    return 1;
                }
                printf("Test 8: Passed :)\n");
                break;

            case 9:
                assert(threads != NULL);
                for (i = 0; i < numthreads; i++) {
                    pthread_create(&threads[i], NULL, thread_main, NULL);
                }

                for (i = 0; i < numthreads; i++) {
                    int *p;
                    pthread_join(threads[i], (void **) &p);
                    if (*p == 0) {
                        printf("Test 9 (Threaded test without coalesce): failed during threads :(\n");
                        return 1;
                    }
                    free(p);
                }

                printf("Test 9 (Threaded test without coalesce): Looks like everything is working :)\n");
                break;

            case 10:
                load.num_of_allocs = 1000;
                load.block_size = 16;
                load.coalesce_order = (int *) malloc(load.num_of_allocs * sizeof(int));
                load.free_order = (int *) malloc(load.num_of_allocs * sizeof(int));

                for (i = 0; i < load.num_of_allocs; i++) {
                    load.free_order[i] = i;
                    load.coalesce_order[i] = 0;
                }

                shuffle(load.free_order, load.num_of_allocs);


                for (i = 0; i < numthreads; i++) {
                    pthread_create(&threads[i], NULL, thread_main2, (void *) &load);
                }

                for (i = 0; i < numthreads; i++) {
                    int *p;
                    pthread_join(threads[i], (void **) &p);
                    if (*p == 0) {
                        printf("Test 10 (Heavy Threaded test without coalesce): Failed during threads :(\n");
                        return 1;
                    }
                    free(p);
                }


                printf("Test 10 (Heavy Threaded test without coalesce): Looks like everything is working :)\n");

                free(load.coalesce_order);
                free(load.free_order);
                break;

            case 11:
                assert(threads != NULL);
                for (i = 0; i < numthreads; i++) {
                    pthread_create(&threads[i], NULL, thread_main, NULL);
                }

                for (i = 0; i < numthreads; i++) {
                    int *p;
                    pthread_join(threads[i], (void **) &p);
                    coalesce_freelist();
                    if (*p == 0) {
                        printf("Test 11 (Threaded test): failed during threads :(\n");
                        return 1;
                    }
                    free(p);
                }

                freelist[0] = HEAPSIZE - nodesize;
                if (complete_state_check(__head, freelist, 1, ptr, 0, sizes)) {
                    printf("Test 11 (Thread test): Looks like everything is working :)\n");
                } else {
                    printf("Test 11 (Thread test): failed after threads :(\n");
                }
                break;

            case 12:
                load.num_of_allocs = 1000;
                load.block_size = 16;
                load.coalesce_order = (int *) malloc(load.num_of_allocs * sizeof(int));
                load.free_order = (int *) malloc(load.num_of_allocs * sizeof(int));

                for (i = 0; i < load.num_of_allocs; i++) {
                    load.free_order[i] = i;
                    load.coalesce_order[i] = rand() % 2;
                }

                shuffle(load.free_order, load.num_of_allocs);


                for (i = 0; i < numthreads; i++) {
                    pthread_create(&threads[i], NULL, thread_main2, (void *) &load);
                }

                for (i = 0; i < numthreads; i++) {
                    int *p;
                    pthread_join(threads[i], (void **) &p);
                    if (*p == 0) {
                        printf("Test 12 (Heavy Thread test): Failed during threads :(\n");
                        return 1;
                    }
                    free(p);
                }

                coalesce_freelist();


                freelist[0] = HEAPSIZE - nodesize;
                if (complete_state_check(__head, freelist, 1, ptr, 0, sizes)) {
                    printf("Test 12 (Heavy Thread test): Looks like everything is working :)\n");
                } else {
                    printf("Test 12 (Heavy Thread test): Failed after threads joined :(\n");
                    return 1;
                }

                free(load.coalesce_order);
                free(load.free_order);
                break;
        }
        destroy_heap();
    }

    free(threads);
    clock_gettime(CLOCK_REALTIME, &end);
    double elapsed = diff2float(&start, &end);
    printf("Time: %f seconds with seed: %d\n", elapsed, seed);

    return 0;
}
