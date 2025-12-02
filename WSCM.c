#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include <errno.h>

#define HIGH_PRIORITY_BUFFER_SIZE 20
#define LOW_PRIORITY_BUFFER_SIZE 20
#define TOTAL_ITEMS 40

int buffer_high[HIGH_PRIORITY_BUFFER_SIZE];
int buffer_low[LOW_PRIORITY_BUFFER_SIZE];
int count_high = 0;
int count_low = 0;

pthread_mutex_t mutex_high, mutex_low;
pthread_mutex_t state_mutex;
pthread_rwlock_t inventory_lock;
pthread_spinlock_t stats_lock;

sem_t sem_full_high, sem_empty_high;
sem_t sem_full_low, sem_empty_low;

int produced_items = 0;
int consumed_items = 0;
int total_operations = 0;
atomic_int alert_counter = 0; 

pthread_t producers[3], consumers[3], audit_thread;
atomic_int stop_threads = 0;

void print_colored(const char* msg, const char* color_code) 
{
    printf("%s%s\033[0m", color_code, msg);
}

void update_stats() 
{
    pthread_spin_lock(&stats_lock);
    total_operations++;
    pthread_spin_unlock(&stats_lock);
}

void log_audit() 
{
    pthread_rwlock_rdlock(&inventory_lock);
    pthread_spin_lock(&stats_lock);

    printf("\033[36m[AUDIT] High:%d | Low:%d | Ops:%d\033[0m\n",
        count_high, count_low, total_operations);

    pthread_spin_unlock(&stats_lock);
    pthread_rwlock_unlock(&inventory_lock);
}

void alert_check() 
{
    int current_val = atomic_fetch_add(&alert_counter, 1) + 1;
    
    if (current_val % 5 != 0) return;

    if (pthread_rwlock_tryrdlock(&inventory_lock) != 0)
        return;

    if (count_low < 2)
        print_colored("[ALERT] Low-priority stock low!\n", "\033[33m");

    if (count_high < 1)
        print_colored("[ALERT] High-priority stock low!\n", "\033[31m");

    pthread_rwlock_unlock(&inventory_lock);
}

int perform_produce(int id, unsigned int* seed) 
{
    int is_high = rand_r(seed) % 2;

    if (is_high) 
    {
        if (sem_trywait(&sem_empty_high) != 0) return -1;
    }
    
    else 
    {
        if (sem_trywait(&sem_empty_low) != 0) return -1;
    }

    pthread_mutex_lock(&state_mutex);
    if (produced_items >= TOTAL_ITEMS) 
    {
        pthread_mutex_unlock(&state_mutex);

        if (is_high) 
        {
        	sem_post(&sem_empty_high);
        }
        
        else
        {
        	sem_post(&sem_empty_low);
        }

        return 0;
    }
    
    int item = ++produced_items;
    pthread_mutex_unlock(&state_mutex);

    if (is_high) 
    {
        pthread_mutex_lock(&mutex_high);
        pthread_rwlock_wrlock(&inventory_lock);

        buffer_high[count_high++] = item;

        pthread_rwlock_unlock(&inventory_lock);
        pthread_mutex_unlock(&mutex_high);

        sem_post(&sem_full_high);

        printf("\033[32m[Producer %d] Produced %d (HIGH)\033[0m\n", id, item);
    }
    
    else 
    {
        pthread_mutex_lock(&mutex_low);
        pthread_rwlock_wrlock(&inventory_lock);

        buffer_low[count_low++] = item;

        pthread_rwlock_unlock(&inventory_lock);
        pthread_mutex_unlock(&mutex_low);

        sem_post(&sem_full_low);

        printf("\033[32m[Producer %d] Produced %d (LOW)\033[0m\n", id, item);
    }

    update_stats();
    alert_check();
    
    return 1;
}

int perform_consume(int id) 
{
    int item = -1;
    int got = 0;

    if (sem_trywait(&sem_full_high) == 0) 
    {
        pthread_mutex_lock(&mutex_high);
        pthread_rwlock_wrlock(&inventory_lock);

        item = buffer_high[--count_high];
        got = 1;

        pthread_rwlock_unlock(&inventory_lock);
        pthread_mutex_unlock(&mutex_high);

        sem_post(&sem_empty_high);
    }
    
    else if (sem_trywait(&sem_full_low) == 0) 
    {
        pthread_mutex_lock(&mutex_low);
        pthread_rwlock_wrlock(&inventory_lock);

        item = buffer_low[--count_low];
        got = 1;

        pthread_rwlock_unlock(&inventory_lock);
        pthread_mutex_unlock(&mutex_low);

        sem_post(&sem_empty_low);
    }

    if (got) 
    {
        pthread_mutex_lock(&state_mutex);
        consumed_items++;
        int done = consumed_items >= TOTAL_ITEMS;
        pthread_mutex_unlock(&state_mutex);

        printf("\033[35m[Consumer %d] Consumed %d\033[0m\n", id, item);

        update_stats();
        alert_check();
        return done ? 0 : 1;
    }

    pthread_mutex_lock(&state_mutex);
    int done = consumed_items >= TOTAL_ITEMS;
    pthread_mutex_unlock(&state_mutex);
    return done ? 0 : -1;
}

void* producer_thread(void* arg) 
{
    int id = *(int*)arg; 
    free(arg);
    unsigned int seed = time(NULL) ^ id;

    while (!atomic_load(&stop_threads)) {
        int r = perform_produce(id, &seed);
        
        if (r == 0) break;
        
        usleep((rand_r(&seed) % 200000) + 50000);
    }
    return NULL;
}

void* consumer_thread(void* arg) 
{
    int id = *(int*)arg; 
    free(arg);
    unsigned int seed = time(NULL) ^ (id + 100);

    while (!atomic_load(&stop_threads)) 
    {
        int r = perform_consume(id);
        if (r == 0)
        {
        	break;
        }
        
        usleep((rand_r(&seed) % 250000) + 40000);
    }
    
    return NULL;
}

void* auditor_thread(void* arg) 
{
    while (!atomic_load(&stop_threads)) 
    {
        pthread_mutex_lock(&state_mutex);
        int done = consumed_items >= TOTAL_ITEMS;
        pthread_mutex_unlock(&state_mutex);

        if (done) break;

        log_audit();
        sleep(2);
    }
    
    return NULL;
}

void reset_system() 
{
    count_high = count_low = 0;
    produced_items = 0;
    consumed_items = 0;
    atomic_store(&alert_counter, 0);
    total_operations = 0;
    atomic_store(&stop_threads, 0);

    sem_destroy(&sem_full_high);
    sem_destroy(&sem_empty_high);
    sem_destroy(&sem_full_low);
    sem_destroy(&sem_empty_low);

    sem_init(&sem_full_high, 0, 0);
    sem_init(&sem_empty_high, 0, HIGH_PRIORITY_BUFFER_SIZE);

    sem_init(&sem_full_low, 0, 0);
    sem_init(&sem_empty_low, 0, LOW_PRIORITY_BUFFER_SIZE);

    printf("[SYSTEM] Reset complete.\n");
}

void start_automatic_simulation() 
{
    reset_system();

    printf("[INFO] Automatic simulation started.\n");

    for (int i = 0; i < 3; i++)
    {
        int* id = malloc(sizeof(int)); *id = i + 1;
        pthread_create(&producers[i], NULL, producer_thread, id);
    }
    
    for (int i = 0; i < 3; i++)
    {
        int* id = malloc(sizeof(int)); *id = i + 1;
        pthread_create(&consumers[i], NULL, consumer_thread, id);
    }
    
    pthread_create(&audit_thread, NULL, auditor_thread, NULL);

    for (int i = 0; i < 3; i++)
    {
        pthread_join(producers[i], NULL);
        pthread_join(consumers[i], NULL);
    }

    pthread_join(audit_thread, NULL);

    printf("\033[34m[INFO] Simulation Complete.\033[0m\n");
}

void start_manual_simulation() 
{
    reset_system();
    unsigned int manual_seed = time(NULL);

    printf("[INFO] Manual Mode Started.\n");
    printf("[1] Produce | [2] Consume | [3] Audit | [4] Exit\n");

    while (1) 
    {
        pthread_mutex_lock(&state_mutex);
        if (consumed_items >= TOTAL_ITEMS) 
        {
            pthread_mutex_unlock(&state_mutex);
            printf("\n[INFO] All items processed. Returning to menu.\n");
            break;
        }
        
        pthread_mutex_unlock(&state_mutex);

        int cmd;
        printf("Manual > "); 
        scanf("%d", &cmd);

        if (cmd == 1) 
        {
            int res = perform_produce(99, &manual_seed);
            if (res == -1) 
            {
            	printf("\033[31m[WARN] Buffers are Full! Cannot produce.\033[0m\n");
            }
            else if (res == 0) 
            {
            	printf("[INFO] Global production limit reached.\n");
            }
        }
        
        else if (cmd == 2) 
        {
            int res = perform_consume(99);
            if (res == -1) 
            {
            	printf("\033[31m[WARN] Buffers are Empty! Cannot consume.\033[0m\n");
            }
        }
        
        else if (cmd == 3) 
        {
        	log_audit();
        }
        else if (cmd == 4) 
        {
        	break;
        }
        
        else
        {
        	printf("Invalid.\n");
        }
    }
}

int main() 
{
    srand(time(NULL));

    pthread_mutex_init(&mutex_high, NULL);
    pthread_mutex_init(&mutex_low, NULL);
    pthread_mutex_init(&state_mutex, NULL);

    pthread_rwlock_init(&inventory_lock, NULL);
    pthread_spin_init(&stats_lock, PTHREAD_PROCESS_PRIVATE);

    sem_init(&sem_full_high, 0, 0);
    sem_init(&sem_empty_high, 0, HIGH_PRIORITY_BUFFER_SIZE);
    sem_init(&sem_full_low, 0, 0);
    sem_init(&sem_empty_low, 0, LOW_PRIORITY_BUFFER_SIZE);

    while (1) 
    {
        printf("\n===== MENU =====\n");
        printf("1. Automatic Simulation\n");
        printf("2. Manual Simulation\n");
        printf("3. Exit\n> ");

        int choice; 
        scanf("%d", &choice);

        if (choice == 1)
        {
        	start_automatic_simulation();
        }
        
        else if (choice == 2)
        {
        	start_manual_simulation();
        }
        
        else if (choice == 3)
        {
        	break;
        }
    }

    atomic_store(&stop_threads, 1);

    pthread_mutex_destroy(&mutex_high);
    pthread_mutex_destroy(&mutex_low);
    pthread_mutex_destroy(&state_mutex);
    pthread_rwlock_destroy(&inventory_lock);
    pthread_spin_destroy(&stats_lock);

    sem_destroy(&sem_full_high);
    sem_destroy(&sem_empty_high);
    sem_destroy(&sem_full_low);
    sem_destroy(&sem_empty_low);

    printf("Exiting.\n");
    return 0;
}