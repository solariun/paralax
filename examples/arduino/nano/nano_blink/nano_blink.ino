/**
 * @file nano_blink.ino
 * @brief Paralax cooperative threading example for Arduino Nano (ATmega328P).
 *
 * Board  : Arduino Nano (ATmega328P, AVR 8-bit, 2 KB SRAM)
 * Threads: 2 (absolute minimum to stay within tight RAM)
 *   1. LED toggle     — toggles LED_BUILTIN every 500 ms
 *   2. Serial counter — prints an incrementing counter every 1000 ms
 *
 * Stack  : 128 bytes per thread.
 *          With 2 threads that is 512 bytes of stack space plus the Thread
 *          objects themselves.  The ATmega328P only has 2048 bytes of SRAM,
 *          so keep everything minimal.  Avoid String objects and large
 *          local arrays inside run().
 *
 * NOTE: Copy (or symlink) paralax.hpp into your Arduino libraries path,
 *       or adjust the #include path below to match your layout.
 *
 * @copyright Copyright (c) 2026 Gustavo Campos
 * @license MIT License. See LICENSE file for details.
 */

/* Shrink the per-thread stack BEFORE including the framework header. */
#define PARALAX_STACK_SIZE 128

#include "../../../include/paralax.hpp"   // adjust path as needed

/* ------------------------------------------------------------------
 * Time hooks required by Paralax
 * ------------------------------------------------------------------ */
size_t paralax_getTime()            { return millis(); }
void   paralax_sleepTime(size_t ms) { delay(ms);       }

/* ------------------------------------------------------------------
 * Forward-declare all thread structs.
 * The Arduino IDE pre-generates function prototypes; any function
 * that references a struct must see the declaration first.
 * ------------------------------------------------------------------ */
struct BlinkThread;
struct CounterThread;

/* ------------------------------------------------------------------
 * Shared objects
 * ------------------------------------------------------------------ */
static LinkList threadList;

/* ------------------------------------------------------------------
 * Thread: LED toggle (500 ms nice)
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
 * Keep local variables to the bare minimum on AVR.
 * ------------------------------------------------------------------ */
struct CounterThread : public Thread {
    CounterThread(LinkList *list)
        : Thread(list, 1000, 120) {}

    void run() override {
        unsigned long count = 0;
        while (true) {
            Serial.print(F("[Counter] "));   // F() keeps literal in flash
            Serial.println(count);
            count++;
            yield();
        }
    }
};

/* ------------------------------------------------------------------
 * Instances (file-scope, allocated in .bss — no heap)
 * ------------------------------------------------------------------ */
static BlinkThread   blinkThread(&threadList);
static CounterThread counterThread(&threadList);

/* ------------------------------------------------------------------
 * Helper: print the thread list
 * ------------------------------------------------------------------ */
static void printThreadList() {
    Serial.println(F("---- Thread List ----"));
    for (Linkable *node = threadList.begin(); node != threadList.end(); node = node->next) {
        Thread *t = (Thread *)node;
        Serial.print(F("  id=0x"));
        Serial.print((unsigned long)(uintptr_t)t->id(), HEX);
        Serial.print(F("  st="));
        Serial.print(t->state_name());
        Serial.print(F("  n="));
        Serial.print((unsigned long)t->nice());
        Serial.print(F("  stk="));
        Serial.print((unsigned long)t->stack_max());
        Serial.println();
    }
    Serial.println(F("---------------------"));
}

/* ------------------------------------------------------------------
 * Arduino setup / loop
 * ------------------------------------------------------------------ */
void setup() {
    Serial.begin(115200);
    // No "while (!Serial)" — the Nano has a HW UART; it is ready immediately.

    Serial.println(F("=== Paralax — Nano Blink ==="));
    Serial.print(F("Stack/thread: "));
    Serial.print((unsigned long)Thread::STACK_SIZE);
    Serial.println(F(" B"));

    printThreadList();

    // This never returns while threads are alive.
    Thread::schedule(&threadList);

    Serial.println(F("Done."));
}

void loop() {
    // Nothing here — scheduler runs in setup().
}
