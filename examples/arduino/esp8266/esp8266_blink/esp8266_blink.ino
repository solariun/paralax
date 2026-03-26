/**
 * @file esp8266_blink.ino
 * @brief Paralax cooperative threading example for ESP8266.
 *
 * Board  : ESP8266 (Xtensa LX106, single-core, 80 KB SRAM)
 * Threads: 2
 *   1. LED blink      — toggles LED_BUILTIN every 500 ms
 *   2. Serial counter — prints an incrementing counter every 1000 ms
 *
 * Stack  : 2048 bytes per thread.
 *          The ESP8266 has ~80 KB of SRAM, but the SDK and WiFi stack
 *          consume a large portion.  2 KB per Paralax thread is a safe
 *          default; reduce if you need more threads.
 *
 * Watchdog timer (WDT) note
 * -------------------------
 * The ESP8266 has a software watchdog that resets the chip if not fed
 * within ~3.2 seconds.  The Arduino delay() function calls
 * optimistic_yield() internally, which feeds the WDT.  Because
 * paralax_sleepTime() is implemented as delay(), the watchdog is
 * automatically serviced whenever the Paralax scheduler idles between
 * thread activations.  As long as no single thread blocks the CPU for
 * more than a few seconds without calling yield(), the WDT will not
 * fire.
 *
 * NOTE: On the ESP8266, LED_BUILTIN is typically GPIO2 and is
 *       active-LOW (LED on when pin is LOW).
 *
 * NOTE: Copy (or symlink) paralax.hpp into your Arduino libraries path,
 *       or adjust the #include path below to match your layout.
 *
 * @copyright Copyright (c) 2026 Gustavo Campos
 * @license MIT License. See LICENSE file for details.
 */

/* Tune the per-thread stack BEFORE including the framework header. */
#define PARALAX_STACK_SIZE 2048

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

/* ------------------------------------------------------------------
 * Shared objects
 * ------------------------------------------------------------------ */
static LinkList threadList;

/* ------------------------------------------------------------------
 * Thread: LED blink (500 ms nice)
 *
 * ESP8266 LED_BUILTIN is typically active-LOW.
 * ------------------------------------------------------------------ */
struct BlinkThread : public Thread {
    BlinkThread(LinkList *list)
        : Thread(list, 500, 100) {}

    void run() override {
        pinMode(LED_BUILTIN, OUTPUT);
        bool ledState = false;
        while (true) {
            ledState = !ledState;
            digitalWrite(LED_BUILTIN, ledState ? LOW : HIGH); // active-LOW
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
 * Instances (file-scope, allocated in .bss — no heap)
 * ------------------------------------------------------------------ */
static BlinkThread   blinkThread(&threadList);
static CounterThread counterThread(&threadList);

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
    // ESP8266 boot messages come out at 74880 baud; a brief delay
    // lets the USB-serial settle before we start printing at 115200.
    delay(200);

    Serial.println();
    Serial.println("=== Paralax — ESP8266 Blink ===");
    Serial.print("Stack per thread: ");
    Serial.print((unsigned long)Thread::STACK_SIZE);
    Serial.println(" bytes");
    Serial.println("WDT: delay() feeds the watchdog automatically.");

    printThreadList();

    // This never returns while threads are alive.
    Thread::schedule(&threadList);

    Serial.println("All threads finished.");
}

void loop() {
    // Nothing here — scheduler runs in setup().
}
