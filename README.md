# Paralax -- Portable Cooperative Threading Framework

A minimal, zero-dependency cooperative threading library for C++11.
Runs on bare metal, RTOS environments, and desktop OSes with the same source code.

## Overview

Paralax provides cooperative (non-preemptive) multithreading using only standard
C++ and a single inline assembly instruction per architecture.  Key properties:

- **No OS required** -- runs on bare-metal microcontrollers with no operating system.
- **No heap allocation** -- all stacks are embedded inside the Thread object as fixed-size arrays.
- **No external dependencies** -- only `<setjmp.h>`, `<stdint.h>`, and `<stddef.h>`.
- **One inline ASM instruction per architecture** -- `set_sp()` writes the stack pointer register; everything else is portable C++ (`setjmp`/`longjmp`).
- **Time-based scheduling** with configurable `nice` (minimum interval) and `priority` (tiebreaker).
- **Wait/notify IPC** -- threads block on `wait(key, param)` and are woken by `notify(key, param, value)`.  Built-in primitives: `Mutex`, `Semaphore`, `Mailbox`, `Queue`.

## Supported Architectures

| Architecture | Typical Platforms | `set_sp` instruction | Notes |
|---|---|---|---|
| **AArch64** | Raspberry Pi 3-5, Apple Silicon (M1-M4) | `mov sp, Xn` | PAC/BTI transparent to setjmp on Apple Clang |
| **ARM / Thumb** | Raspberry Pi 1-2, Pi Pico RP2040/RP2350, STM32, nRF52 | `mov sp, Rn` | Works in both ARM and Thumb mode |
| **RISC-V** | ESP32-C3/C6, SiFive, Milk-V, Pi Pico 2 (Hazard3) | `mv sp, Rn` | RV32 and RV64, any extensions |
| **Xtensa** | ESP32, ESP32-S2, ESP32-S3 | `mov a1, An` | `a1` is the Xtensa stack pointer |
| **Xtensa LX106** | ESP8266 | `mov a1, An` | Same instruction; see ESP8266 notes below |
| **x86-64** | Linux, macOS, Windows (GCC/Clang) | `mov Rn, %rsp` | Desktop development and testing |
| **x86 (32-bit)** | Legacy x86, some embedded | `mov Rn, %esp` | 32-bit mode |
| **AVR** | Arduino Uno, Mega, Nano | `out SP_H/SP_L` | Two 8-bit I/O writes; see AVR notes below |

All architectures share the same source files.  The correct instruction is
selected at compile time via `#if defined(...)` guards.

## Quick Start

```cpp
#include "paralax.hpp"
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* --- User-provided time functions (milliseconds) --- */
size_t paralax_getTime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (size_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void paralax_sleepTime(size_t ms) {
    usleep((uint32_t)(ms * 1000));
}

/* --- Threads --- */
struct Blinker : Thread {
    const char *name;
    int reps;

    Blinker(const char *n, int r, LinkList *list, size_t nice)
        : Thread(list, nice), name(n), reps(r) {}

    void run() override {
        for (int i = 0; i < reps; i++) {
            printf("[%s] tick %d\n", name, i);
            yield();
        }
    }
};

int main() {
    LinkList threads;
    Blinker fast("Fast", 6, &threads,  50);
    Blinker slow("Slow", 3, &threads, 200);
    Thread::schedule(&threads);
}
```

Build and run:

```bash
g++ -std=c++11 -Iinclude -O2 -o demo src/paralax.cpp demo.cpp && ./demo
```

## User-Provided Functions

Paralax does not link against any platform timer.  You supply two `extern "C++"`
functions that define the time unit for your application:

```cpp
extern size_t paralax_getTime();     // return current time
extern void   paralax_sleepTime(size_t time); // sleep for 'time' units
```

Both functions must use the **same** time unit.  The scheduler calls
`paralax_getTime()` to read the clock and `paralax_sleepTime(delta)` to idle
when the next thread is not yet due.

### Desktop (POSIX)

```cpp
#include <time.h>
#include <unistd.h>

size_t paralax_getTime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (size_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void paralax_sleepTime(size_t ms) {
    usleep((uint32_t)(ms * 1000));
}
```

### Arduino

```cpp
size_t paralax_getTime()            { return millis(); }
void   paralax_sleepTime(size_t ms) { delay(ms); }
```

### FreeRTOS

```cpp
size_t paralax_getTime()            { return xTaskGetTickCount(); }
void   paralax_sleepTime(size_t tk) { vTaskDelay(tk); }
```

The time unit here is FreeRTOS ticks. Set `nice` values in ticks accordingly
(e.g., `nice = pdMS_TO_TICKS(100)` for 100 ms).

## API Reference

### Thread

```cpp
class Thread : public Linkable {
public:
    // Construction
    Thread(LinkList *list = nullptr, size_t nice = 0, uint8_t priority = 128);

    // States
    enum State : uint8_t {
        CREATED, READY, RUNNING, YIELDED, WAITING, NOW, FINISHED
    };

    // Accessors
    State       state()      const;  // current state
    const char *state_name() const;  // human-readable state string
    size_t      nice()       const;  // minimum activation interval
    uint8_t     priority()   const;  // tiebreaker (lower = higher priority)
    size_t      next_run()   const;  // next scheduled activation time
    bool        finished()   const;  // true if run() returned
    const void *id()         const;  // opaque object address
    size_t      stack_max()  const;  // PARALAX_STACK_SIZE
    size_t      stack_used() const;  // high-water mark bytes used
    size_t      stack_free() const;  // stack_max() - stack_used()

    static Thread *running();        // pointer to currently executing thread

    // Cooperation
    virtual void run() = 0;          // implement in subclass
    virtual void yield();            // suspend, return to scheduler

    // Wait / Notify IPC
    size_t wait(const void *key, size_t param);
    static bool   notify(const void *key, size_t param, size_t value = 0);
    static size_t notify_all(const void *key, size_t param, size_t value = 0);

    // Scheduler entry point
    static void schedule(LinkList *list);
};
```

**Constructor parameters:**
- `list` -- Thread pool to join. Pass `nullptr` and insert manually later.
- `nice` -- Minimum time units between activations. `0` means run as fast as possible.
- `priority` -- Lower value = scheduled first when other criteria are equal. Default `128`.

**`yield()`** -- Saves context via `setjmp`, returns to the scheduler via
`longjmp`.  Execution resumes at the same point on the next time slice.

**`wait(key, param)`** -- Moves the thread to `WAITING` state.  Blocks until
another thread calls `notify(key, param, value)` with matching `key` and
`param`.  Returns the `value` passed by the notifier.

**`notify(key, param, value)`** -- Wakes **one** waiting thread matching
`(key, param)`.  The woken thread enters `NOW` state for immediate dispatch.
Returns `true` if a thread was woken.

**`notify_all(key, param, value)`** -- Like `notify()`, but wakes **all**
matching threads.  Returns the count of woken threads.

**`schedule(list)`** -- Initializes every thread in `list` (paints stack,
swaps SP, saves initial context via `bootstrap()`), then enters the run loop.
Returns when all threads finish or all remaining threads are waiting (deadlock).

### Mutex

```cpp
class Mutex {
public:
    Mutex();
    void lock();             // blocks if locked
    void unlock();           // releases, wakes one waiter
    bool locked() const;
};
```

Cooperative mutual exclusion.  `lock()` spins on `wait(this, 0)` while the
mutex is held.  `unlock()` clears the flag and calls `notify(this, 0)`.

### Semaphore

```cpp
class Semaphore {
public:
    explicit Semaphore(size_t initial = 0, size_t max = SIZE_MAX);
    void   acquire();        // blocks if count == 0
    bool   try_acquire();    // non-blocking, returns false if unavailable
    void   release();        // increments (up to max), wakes one waiter
    size_t count() const;
};
```

### Mailbox

```cpp
class Mailbox {
public:
    Mailbox();
    void   send(size_t msg); // blocks if full
    size_t recv();           // blocks if empty
    bool   empty() const;
    bool   full()  const;
};
```

Single-slot blocking message channel.  `send()` waits on `(this, 1)` if full;
`recv()` waits on `(this, 0)` if empty.  Each side notifies the other on
completion.

### Queue

```cpp
class Queue {
public:
    Queue(size_t *buf, size_t capacity);
    void   push(size_t val); // blocks if full
    size_t pop();            // blocks if empty
    size_t count()    const;
    size_t capacity() const;
    bool   empty()    const;
    bool   full()     const;
};
```

Bounded FIFO backed by a caller-supplied `size_t` array (no heap).  `push()`
waits on `(this, 1)` when full; `pop()` waits on `(this, 0)` when empty.

### LinkList / Linkable

```cpp
class Linkable {
public:
    Linkable(LinkList *c);   // auto-inserts into list (or nullptr)
    virtual ~Linkable();     // auto-removes from list
    void *get();             // returns this as void*
};

class LinkList {
public:
    LinkList();
    void insert(Linkable *n);
    void remove(Linkable *n);
    void sort(bool (*cmp)(Linkable *, Linkable *));
    Linkable *begin() const; // head
    Linkable *end()   const; // nullptr
};
```

Intrusive doubly-linked list.  `Thread` inherits from `Linkable`, so thread
objects auto-register with their pool on construction and auto-remove on
destruction.  `sort()` uses insertion sort (stable, O(n^2) -- fine for typical
thread counts).

## Scheduling Model

### Nice and Priority

- **`nice`** -- The minimum time interval (in user-defined units) between
  successive activations of a thread.  A thread that yields is not eligible
  to run again until `now >= next_run`, where `next_run = last_yield_time + nice`.
  Setting `nice = 0` means "run as soon as possible."

- **`priority`** -- An 8-bit tiebreaker.  **Lower value = higher priority.**
  Only consulted when two threads fall in the same scheduling tier with
  equal `next_run` times.  Default is `128`.

### Five Scheduling Tiers

Every scheduling cycle, the scheduler snapshots the current time, sorts the
thread list, and picks the first runnable thread.  Threads are ordered into
five tiers, from most urgent to least:

| Tier | Condition | Sort within tier | Behavior |
|------|-----------|------------------|----------|
| **0 -- LATE** | `next_run < now` | Most overdue first (smallest `next_run`), then priority | Runs immediately -- this thread is past due |
| **1 -- NOW** | `state == NOW` | Priority only | Runs immediately -- just notified via `wait/notify` |
| **2 -- On-time** | `next_run >= now` | Earliest `next_run` first, then priority | Scheduler sleeps until `next_run`, then runs |
| **3 -- WAITING** | `state == WAITING` | Skipped | Blocked on `wait()`, cannot run |
| **3 -- FINISHED** | `state == FINISHED` | Skipped | `run()` returned, permanently inactive |

### Sleep Behavior

When the next eligible thread is on-time (tier 2), the scheduler calls
`paralax_sleepTime(next_run - now)` to avoid busy-waiting.  LATE and NOW
threads are dispatched without sleeping.

### Deadlock Detection

The scheduler exits its loop when either:
- All threads are `FINISHED` (normal termination), or
- All non-finished threads are `WAITING` (deadlock -- no thread can make
  progress because none will call `notify()`).

There is no timeout or watchdog -- the scheduler simply returns from
`schedule()`.

## Stack Configuration

Each `Thread` object embeds a `uint8_t stack[PARALAX_STACK_SIZE]` array.
Override the default before including the header:

```cpp
#define PARALAX_STACK_SIZE 256
#include "paralax.hpp"
```

### Recommended Sizes by Platform

| Platform | Recommended `PARALAX_STACK_SIZE` | Notes |
|---|---|---|
| **AVR** (Uno, Mega, Nano) | 128 -- 256 | Total SRAM is 2 KB (Uno) to 8 KB (Mega). Each thread costs `sizeof(Thread)` + stack. |
| **ESP8266** | 1024 -- 2048 | 80 KB DRAM. WDT requires periodic `yield()` (see platform notes). |
| **Pi Pico / ESP32** | 4096 -- 8192 | 264 KB (Pico) or 520 KB (ESP32) SRAM. Generous but not infinite. |
| **Desktop** (x86-64, AArch64) | 8192+ | Default is 8192. Increase for deep call stacks or large locals. |

### Stack Watermarking

During initialization, `schedule()` paints each thread's stack buffer with
`0xCC`.  After execution, `stack_used()` scans from the bottom for intact
watermark bytes to report the high-water mark.  Use this to right-size your
stacks:

```cpp
printf("stack: %zu / %zu used\n", t->stack_used(), t->stack_max());
```

## Platform Notes

### ESP8266 (Xtensa LX106)

The ESP8266 software watchdog timer (WDT) fires if the system does not return
to the SDK event loop for too long.  Make sure threads call `yield()` frequently
(every ~20 ms or less).  If you use `delay()` as your `paralax_sleepTime()`,
the Arduino core's `delay()` feeds the WDT internally.  Avoid long-running
computations without yielding.

### ESP32 (FreeRTOS Coexistence)

On ESP32 under Arduino, user code runs inside a FreeRTOS task.  Paralax threads
are cooperative *within* that FreeRTOS task -- they do not interfere with
FreeRTOS scheduling.  Use `xTaskGetTickCount()` / `vTaskDelay()` for tick-based
timing, or `millis()` / `delay()` for millisecond timing.

### AVR (Arduino Uno / Mega / Nano)

- Total SRAM: 2 KB (Uno/Nano), 8 KB (Mega).  Each thread consumes
  `sizeof(Thread)` + `PARALAX_STACK_SIZE` bytes.  Two threads with 256-byte
  stacks require approximately 600+ bytes total.
- The AVR `set_sp()` uses two 8-bit `out` instructions (`SP_H`, `SP_L`).
  Interrupts should be disabled during `schedule()` if ISRs touch the stack
  (cooperative threads typically do not need ISRs).
- Avoid `printf` -- use `Serial.print()` to save flash and RAM.

### Apple Silicon (AArch64 with PAC/BTI)

Apple Clang's `setjmp`/`longjmp` handle pointer authentication transparently.
No special flags are required.  The `mov sp, Xn` instruction works without PAC
stripping because the stack pointer is not a signed register.

## Building

### Desktop (Linux / macOS)

```bash
make          # builds build/paralax (runs examples/desktop/main.cpp)
make test     # builds and runs tests/paralax_test.cpp
make clean    # removes build/
```

The Makefile uses `g++` with `-std=c++11 -Wall -Wextra -O3 -Iinclude`.

### Arduino

Copy `include/paralax.hpp` and `src/paralax.cpp` into your sketch folder (or
install as a library).  Include the header and define `paralax_getTime()` and
`paralax_sleepTime()` in your sketch:

```cpp
#include "paralax.hpp"

size_t paralax_getTime()            { return millis(); }
void   paralax_sleepTime(size_t ms) { delay(ms); }
```

Set `PARALAX_STACK_SIZE` before the `#include` if the default (8192) is too
large for your board.

### PlatformIO / CMake

Add `include/` to your include path and compile `src/paralax.cpp` alongside
your application sources.

## Project Structure

```
paralax/
  include/
    paralax.hpp          -- public header (all classes + set_sp)
  src/
    paralax.cpp          -- implementation
  examples/
    desktop/
      main.cpp           -- desktop demo (scheduling + semaphore)
    arduino/
      esp32/             -- ESP32 example sketches
      esp8266/           -- ESP8266 example sketches
      nano/              -- Arduino Nano example sketches
      pico/              -- Raspberry Pi Pico example sketches
  tests/
    paralax_test.cpp     -- unit / integration tests
  build/                 -- build output (generated)
  Makefile               -- desktop build rules
  LICENSE                -- MIT license
  README.md              -- this file
  diagram.md             -- architecture diagrams (Mermaid)
```

## License

[MIT](LICENSE) -- Copyright (c) 2026 Gustavo Campos.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files, to deal in the Software
without restriction, including without limitation the rights to use, copy,
modify, merge, publish, distribute, sublicense, and/or sell copies of the
Software.  See [LICENSE](LICENSE) for full terms.
