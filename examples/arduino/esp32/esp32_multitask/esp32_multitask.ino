/**
 * @file esp32_multitask.ino
 * @brief Paralax cooperative threading example for ESP32.
 *
 * Board  : ESP32 (Xtensa LX6 dual-core, 520 KB SRAM)
 * Threads: 4
 *   1. LED blink            — toggles LED_BUILTIN every 500 ms
 *   2. Serial counter       — prints an incrementing counter every 1000 ms
 *   3. Semaphore worker A   — acquires a counting semaphore, uses a shared
 *                             resource, then releases
 *   4. Semaphore worker B   — same, demonstrates mutual exclusion via Semaphore
 *
 * Stack  : default PARALAX_STACK_SIZE (8192 bytes) — ESP32 has plenty of RAM.
 *
 * FreeRTOS coexistence note
 * -------------------------
 * The ESP32 Arduino core is built on top of FreeRTOS. Paralax runs
 * cooperatively inside a single FreeRTOS task (the Arduino "loopTask").
 * All Paralax threads share that one FreeRTOS task and its native stack.
 * This means:
 *   - Do NOT call vTaskDelay / xSemaphoreTake etc. from inside a Paralax
 *     thread; use Paralax primitives instead (yield, Semaphore, Mutex...).
 *   - The ESP32 WDT is fed automatically by delay() which is what
 *     paralax_sleepTime() calls, so no special handling is needed.
 *   - WiFi / BLE callbacks run in other FreeRTOS tasks and are safe,
 *     but shared data accessed from both Paralax threads AND ISRs/other
 *     FreeRTOS tasks must be protected with volatile or real mutexes.
 *
 * NOTE: Copy (or symlink) paralax.hpp into your Arduino libraries path,
 *       or adjust the #include path below to match your layout.
 *
 * @copyright Copyright (c) 2026 Gustavo Campos
 * @license MIT License. See LICENSE file for details.
 */

#include "paralax.hpp"

/* ESP32 boards may not define LED_BUILTIN — GPIO 2 is common */
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

/* ------------------------------------------------------------------
 * Time hooks required by Paralax
 * ------------------------------------------------------------------ */
size_t paralax_getTime()            { return millis(); }
void   paralax_sleepTime(size_t ms) { delay(ms);       }

/* ------------------------------------------------------------------
 * Forward-declare all thread structs.
 * The Arduino IDE pre-generates function prototypes, so any function
 * that references a struct must see its declaration first.
 * ------------------------------------------------------------------ */
struct BlinkThread;
struct CounterThread;
struct WorkerThread;

/* ------------------------------------------------------------------
 * Shared objects
 * ------------------------------------------------------------------ */
static LinkList  threadList;
static Semaphore sem(1);     // binary semaphore (max 1 holder at a time)

// A "shared resource" — just a counter that both workers increment.
static volatile unsigned long sharedCounter = 0;

/* ------------------------------------------------------------------
 * Thread: LED blink (500 ms nice)
 * ------------------------------------------------------------------ */
struct BlinkThread : public Thread {
    BlinkThread(LinkList *list)
        : Thread(list, PARALAX_STACK_SIZE, 500, 100) {}

    void run() override {
        pinMode(LED_BUILTIN, OUTPUT);
        bool ledState = false;
        while (true) {
            ledState = !ledState;
            digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
            yield();
        }
    }
};

/* ------------------------------------------------------------------
 * Thread: Serial counter (1000 ms nice)
 * ------------------------------------------------------------------ */
struct CounterThread : public Thread {
    CounterThread(LinkList *list)
        : Thread(list, PARALAX_STACK_SIZE, 1000, 120) {}

    void run() override {
        unsigned long count = 0;
        while (true) {
            Serial.print("[Counter] count = ");
            Serial.print(count);
            Serial.print("  shared = ");
            Serial.println(sharedCounter);
            count++;
            yield();
        }
    }
};

/* ------------------------------------------------------------------
 * Thread: Semaphore-guarded worker (reused for A and B)
 * ------------------------------------------------------------------ */
struct WorkerThread : public Thread {
    const char *name;

    WorkerThread(LinkList *list, const char *n, uint8_t prio)
        : Thread(list, PARALAX_STACK_SIZE, 300, prio), name(n) {}

    void run() override {
        while (true) {
            sem.acquire();

            // --- critical section ---
            unsigned long before = sharedCounter;
            sharedCounter++;
            Serial.print("[");
            Serial.print(name);
            Serial.print("] acquired sem, counter ");
            Serial.print(before);
            Serial.print(" -> ");
            Serial.println(sharedCounter);
            // --- end critical section ---

            sem.release();
            yield();
        }
    }
};

/* ------------------------------------------------------------------
 * Instances (file-scope; stacks are heap-allocated by the framework)
 * ------------------------------------------------------------------ */
static BlinkThread   blinkThread(&threadList);
static CounterThread counterThread(&threadList);
static WorkerThread  workerA(&threadList, "WorkerA", 130);
static WorkerThread  workerB(&threadList, "WorkerB", 131);

/* ------------------------------------------------------------------
 * Helper: print the thread list
 * ------------------------------------------------------------------ */
static void printThreadList() {
    Serial.println("---- Thread List ----");
    for (Linkable *node = threadList.begin(); node != threadList.end(); node = node->next) {
        Thread *t = (Thread *)node;
        Serial.print("  id=0x");
        Serial.print((unsigned long)(uintptr_t)t->id(), HEX);
        Serial.print("  state=");
        Serial.print(t->state_name());
        Serial.print("  nice=");
        Serial.print((unsigned long)t->nice());
        Serial.print("  prio=");
        Serial.print(t->priority());
        Serial.print("  stack=");
        Serial.print((unsigned long)t->stack_max());
        Serial.println();
    }
    Serial.println("---------------------");
}

/* ------------------------------------------------------------------
 * Arduino setup / loop
 * ------------------------------------------------------------------ */
void setup() {
    Serial.begin(115200);
    // ESP32 USB-CDC may need a brief settle; a short delay is enough.
    delay(500);

    Serial.println("=== Paralax — ESP32 Multitask + Semaphore ===");
    Serial.print("Default stack size: ");
    Serial.print((unsigned long)PARALAX_STACK_SIZE);
    Serial.println(" bytes");
    Serial.println("NOTE: Paralax runs inside the Arduino loopTask (FreeRTOS).");

    printThreadList();

    // This never returns while threads are alive.
    Thread::schedule(&threadList);

    Serial.println("All threads finished.");
}

void loop() {
    // Nothing here — scheduler runs in setup().
}
