/**
 * @file pico_blink.ino
 * @brief Paralax cooperative threading example for Raspberry Pi Pico (RP2040).
 *
 * Board  : Raspberry Pi Pico (RP2040, Cortex-M0+, 264 KB SRAM)
 * Threads: 3
 *   1. LED blink        — toggles LED_BUILTIN every 500 ms
 *   2. Serial counter   — prints an incrementing counter every 1000 ms
 *   3. Mailbox producer — sends a value via Mailbox
 *      Mailbox consumer — receives and prints it
 *      (producer + consumer share a Mailbox; they are 2 of the 3 threads,
 *       so the total is actually 4 threads to demonstrate Mailbox)
 *
 * Stack  : default PARALAX_STACK_SIZE (8192 bytes) — plenty of room on RP2040.
 *
 * NOTE: Copy (or symlink) paralax.hpp into your Arduino libraries path,
 *       or adjust the #include path below to match your layout.
 *
 * @copyright Copyright (c) 2026 Gustavo Campos
 * @license MIT License. See LICENSE file for details.
 */

#include "../../../include/paralax.hpp"   // adjust path as needed

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
struct ProducerThread;
struct ConsumerThread;

/* ------------------------------------------------------------------
 * Shared objects
 * ------------------------------------------------------------------ */
static LinkList threadList;
static Mailbox  mailbox;

/* ------------------------------------------------------------------
 * Thread: LED blink (500 ms nice)
 * ------------------------------------------------------------------ */
struct BlinkThread : public Thread {
    BlinkThread(LinkList *list)
        : Thread(list, 500, 100) {}

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
        : Thread(list, 1000, 120) {}

    void run() override {
        unsigned long count = 0;
        while (true) {
            Serial.print("[Counter] count = ");
            Serial.println(count);
            count++;
            yield();
        }
    }
};

/* ------------------------------------------------------------------
 * Thread: Mailbox producer (500 ms nice)
 * ------------------------------------------------------------------ */
struct ProducerThread : public Thread {
    ProducerThread(LinkList *list)
        : Thread(list, 500, 130) {}

    void run() override {
        size_t value = 0;
        while (true) {
            Serial.print("[Producer] sending ");
            Serial.println(value);
            mailbox.send(value);
            value++;
            yield();
        }
    }
};

/* ------------------------------------------------------------------
 * Thread: Mailbox consumer (no nice — wakes on recv)
 * ------------------------------------------------------------------ */
struct ConsumerThread : public Thread {
    ConsumerThread(LinkList *list)
        : Thread(list, 0, 130) {}

    void run() override {
        while (true) {
            size_t msg = mailbox.recv();
            Serial.print("[Consumer] received ");
            Serial.println(msg);
            yield();
        }
    }
};

/* ------------------------------------------------------------------
 * Instances (file-scope, allocated in .bss — no heap)
 * ------------------------------------------------------------------ */
static BlinkThread    blinkThread(&threadList);
static CounterThread  counterThread(&threadList);
static ProducerThread producerThread(&threadList);
static ConsumerThread consumerThread(&threadList);

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
    while (!Serial) {} // Pico USB-CDC: wait for host to open the port

    Serial.println("=== Paralax — Pi Pico Blink + Mailbox ===");
    Serial.print("Stack per thread: ");
    Serial.print((unsigned long)Thread::STACK_SIZE);
    Serial.println(" bytes");

    printThreadList();

    // This never returns while threads are alive.
    Thread::schedule(&threadList);

    Serial.println("All threads finished.");
}

void loop() {
    // Nothing here — scheduler runs in setup().
}
