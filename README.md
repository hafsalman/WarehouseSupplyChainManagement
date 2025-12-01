# 🏭 Warehouse Supply Chain Management System

## Abstract

This project simulates a **multithreaded warehouse supply chain** in C, managing **high and low priority inventory buffers**. It demonstrates **synchronization and concurrency control** using semaphores, mutexes, read–write locks, and spinlocks to ensure data consistency and prevent race conditions. The system supports **automatic and manual simulation modes**, real-time auditing, and alerts for low-stock levels.

---

## Objective

The main goal is to model **real-world warehouse concurrency**:

1. **Producer–Consumer Problem:** Manage multiple producers and consumers with shared buffers.
2. **Reader–Writer Problem:** Enable concurrent auditing with exclusive inventory updates.
3. **Data Consistency:** Prevent race conditions, deadlocks, and starvation.
4. **Simulation Modes:** Automatic multithreaded and manual step-by-step operation.
5. **Real-Time Alerts:** Notify when inventory is critically low.

---

## Methodology

The simulation uses **POSIX Threads (pthreads)** for concurrent operations.

### Synchronization Techniques

* **Semaphores:** Control buffer capacity.
* **Mutex Locks:** Ensure exclusive access to buffers.
* **Read–Write Locks:** Allow multiple concurrent reads and exclusive writes.
* **Spin Locks:** Lightweight locking for fast stats updates.

---

## Getting Started

### Prerequisites

* GCC compiler
* POSIX Threads (`pthread` library)

### Compile the Program

Save the code as `warehouse.c` and run:

```bash
gcc WSCM.c -o warehouse -pthread
```

### Run the Simulation

Execute:

```bash
./warehouse
```

You will see the main menu:

```
===== WAREHOUSE SYSTEM MENU =====
1. Automatic Simulation (Multithreaded)
2. Manual Simulation (Step-by-Step)
3. Exit
Enter your choice:
```

### Simulation Modes

* **Automatic Simulation:** Runs all producers, consumers, and auditor threads until completion.
* **Manual Simulation:** Step-by-step control for producing, consuming, and auditing inventory.
* **Exit:** Ends the program.

---

## Features

* Priority-based buffers for efficient inventory management.
* Real-time auditing and alert system.
* Multithreading with proper synchronization to prevent concurrency issues.
* Flexible simulation modes for demonstration and testing.
