#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define HIGH_PRIORITY_BUFFER_SIZE 3
#define LOW_PRIORITY_BUFFER_SIZE 5
#define TOTAL_ITEMS 20

int buffer_high[HIGH_PRIORITY_BUFFER_SIZE];
int buffer_low[LOW_PRIORITY_BUFFER_SIZE];
int count_high = 0, count_low = 0;

pthread_mutex_t mutex_high, mutex_low;
sem_t sem_full_high, sem_empty_high;
sem_t sem_full_low, sem_empty_low;
pthread_rwlock_t inventory_lock;
pthread_spinlock_t stats_lock;

int produced_items = 0;
int consumed_items = 0;
int total_operations = 0;
int alert_counter = 0;

pthread_t producers[3], consumers[3], audit_thread;
int stop_threads = 0;

void print_colored(const char* msg, const char* color_code) 
{
    printf("%s%s\033[0m", color_code, msg);
}

void reset_system() 
{
    count_high = 0;
    count_low = 0;
    produced_items = 0;
    consumed_items = 0;
    total_operations = 0;
    alert_counter = 0;
    top_threads = 0;

    sem_init(&sem_full_high, 0, 0);
    sem_init(&sem_empty_high, 0, HIGH_PRIORITY_BUFFER_SIZE);
    sem_init(&sem_full_low, 0, 0);
    sem_init(&sem_empty_low, 0, LOW_PRIORITY_BUFFER_SIZE);
        
    printf("[SYSTEM] Simulation State Reset.\n");
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
    printf("\033[36m[AUDIT] Inventory Snapshot | High: %d | Low: %d | Total Ops: %d\033[0m\n", count_high, count_low, total_operations);
    pthread_rwlock_unlock(&inventory_lock);
}

void alert_check() 
{
    alert_counter++;
    if (alert_counter % 5 != 0)
    {
        return;
    }
    
    if (pthread_rwlock_tryrdlock(&inventory_lock) == 0) 
    {
        if (count_low < 2) 
        {
            print_colored("[ALERT]  Low priority stock critically low!\n", "\033[33m");
        }

        if (count_high < 1) 
        {
            print_colored("[ALERT]  High priority stock critically low!\n", "\033[31m");
        }

        pthread_rwlock_unlock(&inventory_lock);
    }
}

int perform_produce(int id) 
{
    pthread_mutex_lock(&mutex_high);

    if (produced_items >= TOTAL_ITEMS) 
    {
        pthread_mutex_unlock(&mutex_high);
        return 0;
    }

    int item = produced_items + 1;

    pthread_mutex_unlock(&mutex_high);

    int is_high = rand() % 2;

    if (is_high) 
    {
        if(sem_trywait(&sem_empty_high) != 0) return -1;
        
        pthread_mutex_lock(&mutex_high);
        buffer_high[count_high++] = ++produced_items;
        printf("\033[32m[Producer %d] Produced item %d (HIGH Priority)\033[0m\n", id, item);
        pthread_mutex_unlock(&mutex_high);
        sem_post(&sem_full_high);
    } 
    
    else 
    {
        if(sem_trywait(&sem_empty_low) != 0) 
        {
            return -1;
        }

        pthread_mutex_lock(&mutex_low);
        buffer_low[count_low++] = ++produced_items;
        printf("\033[32m[Producer %d] Produced item %d (LOW Priority)\033[0m\n", id, item);
        pthread_mutex_unlock(&mutex_low);
        sem_post(&sem_full_low);
    }

    update_stats();
    
    pthread_rwlock_wrlock(&inventory_lock);
    alert_check();
    pthread_rwlock_unlock(&inventory_lock);
    
    return 1;
}

int perform_consume(int id) 
{
    int item = -1;
    int got_item = 0;

    if (sem_trywait(&sem_full_high) == 0) 
    {
        pthread_mutex_lock(&mutex_high);
        if (count_high > 0) 
        {
            item = buffer_high[--count_high];
            got_item = 1;
        }

        pthread_mutex_unlock(&mutex_high);
        sem_post(&sem_empty_high);
    } 
    
    else if (sem_trywait(&sem_full_low) == 0) 
    {
        pthread_mutex_lock(&mutex_low);

        if (count_low > 0) 
        {
            item = buffer_low[--count_low];
            got_item = 1;
        }

        pthread_mutex_unlock(&mutex_low);
        sem_post(&sem_empty_low);
    }

    if (got_item) 
    {
        pthread_mutex_lock(&mutex_high);
        consumed_items++;
        pthread_mutex_unlock(&mutex_high);
        
        printf("\033[35m[Consumer %d] Consumed item %d\033[0m\n", id, item);
        update_stats();
        
        pthread_rwlock_wrlock(&inventory_lock);
        alert_check();
        pthread_rwlock_unlock(&inventory_lock);
        return 1;
    } 
    
    if (consumed_items >= TOTAL_ITEMS) return 0;
    return -1;
}

void* producer_thread(void* arg) 
{
    int id = *(int*)arg;
    free(arg);

    while (!stop_threads) 
    {
        int result = perform_produce(id);
        if (result == 0) break; 

        usleep(rand() % 200000 + 50000); 
    }
    return NULL;
}

void* consumer_thread(void* arg) 
{
    int id = *(int*)arg;
    free(arg);
    
    while (!stop_threads) 
    {
        int result = perform_consume(id);
        if (result == 0) break; 
        usleep(rand() % 300000 + 50000);
    }

    return NULL;
}

void* auditor_thread(void* arg)
{
    while (!stop_threads) 
    {
        if (consumed_items >= TOTAL_ITEMS)
        {
            break;
        }

        log_audit();
        sleep(2);
    }
    return NULL;
}

void start_automatic_simulation() 
{
    reset_system();
    printf("[INFO] Automatic Simulation Started!\n");

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
    printf("\n\033[34m[INFO] Automatic Simulation Completed.\033[0m\n");
}

void start_manual_simulation() 
{
    reset_system();
    printf("[INFO] Manual Simulation Mode Started.\n");
    printf("Controls: [1] Produce Item  [2] Consume Item  [3] Audit  [4] Return to Menu\n");

    int choice;
    while(1) 
    {
        if (consumed_items >= TOTAL_ITEMS) 
        {
            printf("\n\033[34m[INFO] All items processed. Simulation Finished.\033[0m\n");
            break;
        }

        printf("\nManual Cmd > ");
        scanf("%d", &choice);

        if (choice == 1) 
        {
            int res = perform_produce(99);
            if (res == -1) printf("[WARN] Buffers are full!\n");
            else if (res == 0) printf("[INFO] Production limit reached.\n");
        } 

        else if (choice == 2) 
        {
            int res = perform_consume(99);
            if (res == -1) printf("[WARN] Buffers are empty!\n");
        } 
        
        else if (choice == 3) 
        {
            log_audit();
        } 

        else if (choice == 4)
        {
            break;
        } 

        else 
        {
            printf("Invalid Input.\n");
        }
    }
}

int main() 
{
    srand(time(NULL));

    pthread_mutex_init(&mutex_high, NULL);
    pthread_mutex_init(&mutex_low, NULL);
    sem_init(&sem_full_high, 0, 0);
    sem_init(&sem_empty_high, 0, HIGH_PRIORITY_BUFFER_SIZE);
    sem_init(&sem_full_low, 0, 0);
    sem_init(&sem_empty_low, 0, LOW_PRIORITY_BUFFER_SIZE);
    pthread_rwlock_init(&inventory_lock, NULL);
    pthread_spin_init(&stats_lock, PTHREAD_PROCESS_PRIVATE);

    int choice;
    
    while (1) 
    {
        printf("\n===== WAREHOUSE SYSTEM MENU =====\n");
        printf("1. Automatic Simulation (Multithreaded)\n");
        printf("2. Manual Simulation (Step-by-Step)\n");
        printf("3. Exit\n");
        printf("Enter your choice: ");
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
            stop_threads = 1;
            break;
        } 
        
        else 
        {
            printf("Invalid choice. Try again.\n");
        }
    }

    pthread_mutex_destroy(&mutex_high);
    pthread_mutex_destroy(&mutex_low);
    sem_destroy(&sem_full_high);
    sem_destroy(&sem_empty_high);
    sem_destroy(&sem_full_low);
    sem_destroy(&sem_empty_low);
    pthread_rwlock_destroy(&inventory_lock);
    pthread_spin_destroy(&stats_lock);

    printf("Exiting Program.\n");
    return 0;
}