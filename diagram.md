# Paralax Architecture Diagrams

All diagrams use [Mermaid](https://mermaid.js.org/) syntax and render natively
on GitHub.

---

## 1. Class Hierarchy

```mermaid
classDiagram
    class Linkable {
        +Linkable *next
        +Linkable *prev
        +LinkList *container
        +Linkable(LinkList *c)
        +~Linkable()
        +get() void*
    }

    class LinkList {
        +Linkable *head
        +Linkable *tail
        +insert(Linkable *n)
        +remove(Linkable *n)
        +sort(cmp)
        +begin() Linkable*
        +end() Linkable*
    }

    class Thread {
        -jmp_buf ctx
        -jmp_buf caller
        -State _state
        -uint8_t _priority
        -size_t _nice
        -size_t _next_run
        -const void *_wait_key
        -size_t _wait_param
        -size_t _wait_value
        -uint8_t *_stack_ptr
        -size_t _stack_size
        -bool _owns_stack
        +Thread(LinkList*, size_t stack_size, size_t nice, uint8_t priority)
        +Thread(LinkList*, uint8_t *ext_stack, size_t ext_size, size_t nice, uint8_t priority)
        +run()* void
        +yield() void
        +finishing() void
        +on_stack_overflow() void
        +wait(key, param) size_t
        +notify(key, param, value)$ bool
        +notify_all(key, param, value)$ size_t
        +schedule(LinkList*)$ void
        +running()$ Thread*
        +state() State
        +state_name() const char*
        +nice() size_t
        +priority() uint8_t
        +next_run() size_t
        +finished() bool
        +id() const void*
        +stack_max() size_t
        +stack_used() size_t
        +stack_free() size_t
        -set_sp(void*)$ void
        -stack_paint() void
        -bootstrap()$ void
    }

    class Mutex {
        -bool _locked
        +lock() void
        +unlock() void
        +locked() bool
    }

    class Semaphore {
        -size_t _count
        -size_t _max
        +Semaphore(size_t initial, size_t max)
        +acquire() void
        +try_acquire() bool
        +release() void
        +count() size_t
    }

    class Mailbox {
        -size_t _msg
        -bool _full
        +send(size_t msg) void
        +recv() size_t
        +empty() bool
        +full() bool
    }

    class Queue {
        -size_t *_buf
        -size_t _cap
        -size_t _head
        -size_t _tail
        -size_t _count
        +Queue(size_t *buf, size_t capacity)
        +push(size_t val) void
        +pop() size_t
        +count() size_t
        +capacity() size_t
        +empty() bool
        +full() bool
    }

    Linkable --> LinkList : container
    LinkList o-- Linkable : head / tail
    Thread --|> Linkable : extends

    Mutex ..> Thread : wait/notify
    Semaphore ..> Thread : wait/notify
    Mailbox ..> Thread : wait/notify
    Queue ..> Thread : wait/notify
```

---

## 2. Thread State Machine

```mermaid
stateDiagram-v2
    [*] --> CREATED : constructor

    CREATED --> READY : schedule() init phase

    READY --> RUNNING : scheduler dispatches\n(longjmp into ctx)

    RUNNING --> YIELDED : yield()\n(setjmp ctx, longjmp caller)
    YIELDED --> RUNNING : scheduler resumes\n(longjmp into ctx)

    RUNNING --> WAITING : wait(key, param)\n(setjmp ctx, longjmp caller)
    WAITING --> NOW : notify(key, param, value)\n(another thread calls notify)
    NOW --> RUNNING : scheduler dispatches\n(immediate, no sleep)

    RUNNING --> FINISHING : run() returns
    FINISHING --> FINISHED : finishing() callback completes\n(longjmp caller)

    RUNNING --> OVERFLOW : guard zone corrupted\n(bottom 16 bytes overwritten)
    OVERFLOW --> FINISHED : on_stack_overflow() called\nthread forcibly killed

    FINISHED --> [*]

    state FINISHING {
        [*] --> finishing_cb : finishing() virtual callback
        finishing_cb --> [*] : cleanup complete
    }

    state OVERFLOW {
        [*] --> overflow_cb : on_stack_overflow() virtual callback
        overflow_cb --> [*] : thread killed
    }
```

---

## 3. Scheduler Flow

```mermaid
flowchart TD
    A["schedule(list)"] --> B["Phase 1: Init Loop"]

    subgraph init ["Phase 1 -- Thread Initialization"]
        B --> C["For each Thread t in list"]
        C --> D["stack_paint()\nFill stack with 0xCC"]
        D --> E["state = READY\nnext_run = now"]
        E --> F["_init_target = t"]
        F --> G["setjmp(sched_ret)"]
        G --> H["Compute top = _stack_ptr + _stack_size\nAlign to 16 bytes: top = (top-16) & ~0xF"]
        H --> I["set_sp(top)\nSwitch to thread stack"]
        I --> J["bootstrap()"]
        J --> K["setjmp(t->ctx)\nSave thread context"]
        K --> L["longjmp(sched_ret)\nReturn to scheduler"]
        L --> C
    end

    C -- "all threads initialized" --> M["Phase 2: run_loop(list)"]

    subgraph loop ["Phase 2 -- Scheduler Run Loop"]
        M --> N["_sort_now = paralax_getTime()"]
        N --> O["list->sort(_sched_cmp)\nOrder: LATE > NOW > on-time > WAITING > FINISHED"]
        O --> P["Scan for first runnable thread"]
        P --> Q{Found runnable?}
        Q -- "No: all FINISHED\nor all WAITING" --> R["return\n(exit scheduler)"]
        Q -- "Yes: thread 'next'" --> S{Is next on-time\nand next_run > now?}
        S -- "Yes" --> T["paralax_sleepTime(next_run - now)"]
        S -- "No (LATE or NOW)" --> U["Run immediately"]
        T --> U
        U --> V["setjmp(next->caller)"]
        V --> W["next->state = RUNNING\nlongjmp(next->ctx)"]
        W --> X["--- Thread executes ---\ncalls yield() or wait()\nor run() returns"]
        X --> GZ{"Guard zone check:\n_stack_ptr[0..15]\nstill 0xCC?"}
        GZ -- "No: corrupted" --> OVF["on_stack_overflow()\nstate = FINISHED\n(thread killed)"]
        OVF --> N
        GZ -- "Yes: intact" --> Y{"Did run() return?"}
        Y -- "Yes" --> FIN["finishing() callback\nstate = FINISHED"]
        FIN --> N
        Y -- "No (yield/wait)" --> YS["set state = YIELDED\nif still RUNNING"]
        YS --> Z["next_run = now + nice"]
        Z --> N
    end
```

---

## 4. Wait / Notify Sequence

```mermaid
sequenceDiagram
    participant A as Thread A
    participant Sched as Scheduler
    participant B as Thread B

    Note over A: Needs a resource held by B

    A ->> A: wait(key, param)
    Note over A: _wait_key = key<br>_wait_param = param<br>state = WAITING
    A ->> Sched: setjmp(ctx), longjmp(caller)
    Note over Sched: Thread A is now WAITING<br>Skipped during scheduling

    Sched ->> B: longjmp(B.ctx)
    Note over B: B runs, produces result

    B ->> B: notify(key, param, value)
    Note over B: Scans thread list for<br>WAITING thread matching<br>(key, param)
    Note over B: Found Thread A:<br>A._wait_value = value<br>A.state = NOW

    B ->> B: yield()
    B ->> Sched: setjmp(ctx), longjmp(caller)

    Note over Sched: Sorts threads<br>A is NOW (tier 1) -- high urgency

    Sched ->> A: longjmp(A.ctx)
    Note over A: Resumes from wait()<br>state = RUNNING<br>returns value
```

---

## 5. Memory Layout

### 5a. Framework-Allocated Stack (default constructor)

```mermaid
block-beta
    columns 1

    block:threadobj["Thread Object (sizeof(Thread)) -- no embedded buffer"]
        columns 5
        vtable["vtable ptr"]:1
        linkable["Linkable fields\n(next, prev, container)"]:1
        jmpbufs["jmp_buf ctx\njmp_buf caller"]:1
        statefields["state | priority\nnice | next_run\nwait_key | wait_param\nwait_value"]:1
        stackmeta["_stack_ptr\n_stack_size\n_owns_stack = true"]:1
    end

    space

    block:heapbuf["Heap buffer: new uint8_t[stack_size] (freed on destruction)"]
        columns 8
        guard["buf[0..15]\nGUARD ZONE\n(16 bytes)"]:2
        low["buf[16..]\n0xCC watermark\nuntouched"]:2
        used["...\nused by\nthread\nexecution"]:2
        high["buf[N-1]\n(high addr)\nset_sp points\nhere - 16"]:2
    end

    threadobj --> heapbuf

    style guard fill:#d4a,color:#fff
    style low fill:#4a9,color:#fff
    style used fill:#e55,color:#fff
    style high fill:#47c,color:#fff
```

### 5b. User-Provided Stack (external-stack constructor)

```mermaid
block-beta
    columns 1

    block:threadobj2["Thread Object (sizeof(Thread)) -- no embedded buffer"]
        columns 5
        vtable2["vtable ptr"]:1
        linkable2["Linkable fields\n(next, prev, container)"]:1
        jmpbufs2["jmp_buf ctx\njmp_buf caller"]:1
        statefields2["state | priority\nnice | next_run\nwait_key | wait_param\nwait_value"]:1
        stackmeta2["_stack_ptr\n_stack_size\n_owns_stack = false"]:1
    end

    space

    block:extstackbuf["uint8_t *ext_stack (static array / custom memory, size = ext_size, never freed)"]
        columns 8
        extguard["ext[0..15]\nGUARD ZONE\n(16 bytes)"]:2
        extlow["ext[16..]\n0xCC watermark\nuntouched"]:2
        extused["...\nused by\nthread\nexecution"]:2
        exthigh["ext[N-1]\n(high addr)\nset_sp points\nhere - 16"]:2
    end

    threadobj2 --> extstackbuf

    style extguard fill:#d4a,color:#fff
    style extlow fill:#4a9,color:#fff
    style extused fill:#e55,color:#fff
    style exthigh fill:#47c,color:#fff
```

**Stack layout details:**

- **Framework-allocated (default):** The constructor allocates `new uint8_t[stack_size]`. `_stack_ptr` points to the heap buffer, `_stack_size` holds the size, and `_owns_stack = true` so the destructor calls `delete[]`.
- **User-provided:** When constructed with `Thread(list, ext_stack, ext_size, nice, priority)`, `_stack_ptr` and `_stack_size` point to the caller-supplied buffer. `_owns_stack = false`, so the destructor never frees the buffer.
- There is **no embedded `_stack_buf[]` array** inside the Thread object. The Thread only stores a pointer, a size, and the ownership flag.
- `set_sp()` points the hardware stack pointer to `(_stack_ptr + _stack_size - 16) & ~0xF` (16-byte aligned, with 16 bytes of headroom).
- The stack **grows downward** from high addresses toward low addresses.
- `stack_paint()` fills the stack buffer (heap or external) with `0xCC` before first use.
- **Guard zone:** The bottom 16 bytes (`_stack_ptr[0..15]`) serve as a guard zone. If these bytes are overwritten (no longer `0xCC`), the scheduler detects a stack overflow and calls `on_stack_overflow()`, then forcibly kills the thread.
- `stack_used()` scans from `_stack_ptr[0]` upward, counting intact `0xCC` bytes. The first non-`0xCC` byte marks the high-water boundary. `stack_used = _stack_size - clean_bytes`.
- `stack_free() = stack_max() - stack_used()`.

```
Address:  low ──────────────────────────────────────────────────── high

          [ GUARD 16B | 0xCC 0xCC ... | used frames ... | ← SP ]
          ↑            ↑               ↑                   ↑
          _stack_ptr   guard end       watermark boundary   set_sp target
          [0]          [16]            (stack_used)         (top - 16, aligned)
```
