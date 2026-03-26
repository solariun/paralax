/**
 * @file paralax.hpp
 * @brief Paralax — Portable cooperative threading framework.
 *
 * @copyright Copyright (c) 2026 Gustavo Campos
 * @license MIT License. See LICENSE file for details.
 *
 * Hybrid approach: minimal inline ASM (one instruction) to set
 * the stack pointer, setjmp/longjmp for full context save/restore.
 *
 * The user MUST define two extern functions:
 * - @c paralax_getTime()   — return current time
 * - @c paralax_sleepTime() — sleep for given time units
 *
 * Both use the same time unit (millis, micros, ticks — user decides).
 *
 * Requirements: GCC or Clang, C++11 or later.
 */

#ifndef PARALAX_HPP
#define PARALAX_HPP

#include <setjmp.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Stack watermark byte used to track high-water mark usage.
 */
#define STACK_WATERMARK 0xCC

/**
 * @brief Default per-thread stack size in bytes.
 *
 * Override before including this header for constrained targets:
 * @code
 * #define PARALAX_STACK_SIZE 256  // AVR
 * #include "paralax.hpp"
 * @endcode
 */
#ifndef PARALAX_STACK_SIZE
#define PARALAX_STACK_SIZE 8192
#endif

/* ---------------------------------------------------------------
 * User-provided time functions (must be defined in user code).
 *
 * Examples:
 *   Desktop:
 *     size_t paralax_getTime() { ... clock_gettime ... }
 *     void   paralax_sleepTime(size_t ms) { usleep(ms*1000); }
 *
 *   Arduino:
 *     size_t paralax_getTime() { return millis(); }
 *     void   paralax_sleepTime(size_t ms) { delay(ms); }
 * --------------------------------------------------------------- */

/**
 * @brief Return the current time in user-defined units.
 * @return Current timestamp.
 */
extern size_t paralax_getTime();

/**
 * @brief Sleep for the given duration in the same units as paralax_getTime().
 * @param time Duration to sleep.
 */
extern void paralax_sleepTime(size_t time);

/* =============================================================== */

class LinkList; ///< Forward declaration.

/**
 * @class Linkable
 * @brief Intrusive doubly-linked list node.
 *
 * Any class that participates in a LinkList inherits from Linkable.
 * Auto-inserts on construction, auto-removes on destruction.
 */
class Linkable {
public:
	Linkable	*next;      ///< Next node in the list.
	Linkable	*prev;      ///< Previous node in the list.
	LinkList	*container; ///< Owning list, or nullptr.

	Linkable() = delete;

	/**
	 * @brief Construct and optionally insert into a list.
	 * @param c List to insert into (nullptr to leave unlinked).
	 */
	Linkable(LinkList *c);

	/// @brief Destructor. Removes from the list if linked.
	virtual ~Linkable();

	/**
	 * @brief Return this pointer as void* for derived-type casting.
	 * @return Pointer to this object.
	 */
	void *get() { return (void *)this; }
};

/* =============================================================== */

/**
 * @class LinkList
 * @brief Intrusive doubly-linked list with insertion-sort.
 */
class LinkList {
public:
	Linkable	*head; ///< First node.
	Linkable	*tail; ///< Last node.

	LinkList() : head(nullptr), tail(nullptr) {}

	/**
	 * @brief Append a node at the tail.
	 * @param n Node to insert.
	 */
	void insert(Linkable *n);

	/**
	 * @brief Remove a node from the list.
	 * @param n Node to remove.
	 */
	void remove(Linkable *n);

	/// @brief Iterator to the first node.
	Linkable *begin() const { return head; }

	/// @brief Past-the-end sentinel (always nullptr).
	Linkable *end() const { return nullptr; }

	/**
	 * @brief Insertion-sort the list.
	 * @param cmp Returns true if @p a should come before @p b.
	 */
	void sort(bool (*cmp)(Linkable *, Linkable *));
};

/* =============================================================== */

/**
 * @class Thread
 * @brief Cooperative thread with per-thread stack and time-based scheduling.
 *
 * @par Scheduling order (highest to lowest urgency):
 * 1. **LATE** — next_run < now. Most overdue first.
 * 2. **NOW**  — just notified via wait/notify.
 * 3. **On-time** — next_run >= now. Earliest first.
 * 4. **WAITING** — blocked on wait(), skipped.
 * 5. **FINISHED** — done, skipped.
 *
 * Within any tier, lower priority value wins.
 */
class Thread : public Linkable {
public:
	/// Per-thread stack size in bytes (configurable via PARALAX_STACK_SIZE).
	static constexpr size_t STACK_SIZE = PARALAX_STACK_SIZE;

	/// Guard zone at the bottom of the stack for overflow detection.
	static constexpr size_t STACK_GUARD = 16;

	/// Thread states.
	enum State : uint8_t {
		CREATED  = 0, ///< Constructed, not yet scheduled.
		READY    = 1, ///< Initialized, waiting for CPU.
		RUNNING  = 2, ///< Currently executing.
		YIELDED  = 3, ///< Voluntarily gave up CPU.
		WAITING  = 4, ///< Blocked on wait().
		NOW      = 5, ///< Notified — immediate dispatch.
		FINISHED = 6  ///< run() returned.
	};

private:
	jmp_buf		ctx;
	jmp_buf		caller;
	State		_state;
	uint8_t		_priority;
	size_t		_nice;
	size_t		_next_run;

	const void	*_wait_key;
	size_t		_wait_param;
	size_t		_wait_value;

	uint8_t		*_stack_ptr;   ///< Active stack buffer (internal or external).
	size_t		_stack_size;   ///< Size of the active stack buffer.
	uint8_t		_stack_buf[STACK_SIZE]; ///< Built-in stack (used when no external buffer).

	static jmp_buf sched_ret;
	static Thread *volatile _init_target;
	static Thread *volatile _running;
	static size_t _sort_now;

	/* --- set_sp: the ONLY inline ASM in the framework --- */
	static inline void set_sp(void *sp)
	{
#if defined(__aarch64__)
		asm volatile("mov sp, %0" :: "r"(sp) : "memory");
#elif defined(__arm__) || defined(__thumb__)
		asm volatile("mov sp, %0" :: "r"(sp) : "memory");
#elif defined(__riscv)
		asm volatile("mv sp, %0" :: "r"(sp) : "memory");
#elif defined(__XTENSA__)
		asm volatile("mov a1, %0" :: "r"(sp) : "memory");
#elif defined(__x86_64__) || defined(_M_X64)
		asm volatile("mov %0, %%rsp" :: "r"(sp) : "memory");
#elif defined(__i386__) || defined(_M_IX86)
		asm volatile("mov %0, %%esp" :: "r"(sp) : "memory");
#elif defined(__AVR__)
		uint16_t addr = (uint16_t)(uintptr_t)sp;
		asm volatile(
			"out __SP_H__, %B0 \n\t"
			"out __SP_L__, %A0 \n\t"
			:: "r"(addr) : "memory"
		);
#else
#error "Unsupported architecture — add set_sp() for your platform"
#endif
	}

	void stack_paint();

	/**
	 * @brief Check if the stack guard zone is intact.
	 * @return true if no overflow detected.
	 */
	bool stack_check() const;

	__attribute__((noinline))
	static void bootstrap();

	static int8_t _tier(const Thread *t);
	static bool   _sched_cmp(Linkable *a, Linkable *b);
	static void   run_loop(LinkList *list);

protected:
	/**
	 * @brief Thread entry point — implement in derived classes.
	 *
	 * Call yield() periodically to cooperate with other threads.
	 */
	virtual void run() = 0;

	/**
	 * @brief Called when run() returns, before the thread is marked FINISHED.
	 *
	 * Override to release resources, close handles, or log the exit.
	 * Default implementation does nothing.
	 */
	virtual void finishing() {}

	/**
	 * @brief Called when a stack overflow is detected.
	 *
	 * The scheduler checks the guard zone (bottom STACK_GUARD bytes)
	 * after each time slice. If corrupted, this callback is invoked
	 * and the thread is forcibly marked FINISHED.
	 *
	 * Override to log, blink an LED, or dump diagnostics.
	 * Default implementation does nothing.
	 */
	virtual void on_stack_overflow() {}

public:
	/**
	 * @brief Construct a thread with built-in stack.
	 * @param list  Thread pool list (nullptr to leave unlinked).
	 * @param nice  Minimum interval between activations (time units). 0 = ASAP.
	 * @param priority Tiebreaker: lower value = higher priority. Default 128.
	 */
	Thread(LinkList *list = nullptr, size_t nice = 0, uint8_t priority = 128);

	/**
	 * @brief Construct a thread with an external stack buffer.
	 *
	 * Use this when the stack lives on the heap or in a custom memory region.
	 * The caller owns the buffer and must keep it alive until the thread finishes.
	 *
	 * @param list      Thread pool list (nullptr to leave unlinked).
	 * @param nice      Minimum interval between activations. 0 = ASAP.
	 * @param priority  Tiebreaker: lower = higher priority. Default 128.
	 * @param ext_stack Pointer to the external stack buffer.
	 * @param ext_size  Size of the external buffer in bytes.
	 */
	Thread(LinkList *list, size_t nice, uint8_t priority,
	       uint8_t *ext_stack, size_t ext_size);

	/// @brief Current state.
	State       state()     const { return _state; }

	/// @brief Minimum activation interval.
	size_t      nice()      const { return _nice; }

	/// @brief Priority (lower = higher).
	uint8_t     priority()  const { return _priority; }

	/// @brief Timestamp of next scheduled activation.
	size_t      next_run()  const { return _next_run; }

	/// @brief True if run() has returned.
	bool        finished()  const { return _state == FINISHED; }

	/// @brief Object address as an opaque ID.
	const void *id()        const { return (const void *)this; }

	/// @brief Total stack buffer size (internal or external).
	size_t      stack_max() const { return _stack_size; }

	/// @brief Pointer to the currently executing thread (or nullptr).
	static Thread *running() { return _running; }

	/**
	 * @brief Human-readable state name.
	 * @return One of "CREATED", "READY", "RUNNING", "YIELDED", "WAITING", "NOW", "FINISHED".
	 */
	const char *state_name() const;

	/**
	 * @brief Stack high-water mark — bytes used at peak.
	 *
	 * Scans the watermark pattern from the bottom of the stack.
	 * @return Number of bytes that have been touched.
	 */
	size_t stack_used() const;

	/**
	 * @brief Remaining stack bytes.
	 * @return stack_max() - stack_used().
	 */
	size_t stack_free() const { return _stack_size - stack_used(); }

	/**
	 * @brief Voluntarily suspend this thread.
	 *
	 * Saves context and returns control to the scheduler.
	 * Execution resumes here on the next time slice.
	 */
	virtual void yield();

	/**
	 * @brief Suspend until notified on (key, param).
	 *
	 * The thread enters WAITING state and is skipped by the scheduler.
	 * Another thread must call notify() with matching key and param.
	 *
	 * @param key   Endpoint identifier (pointer to any object).
	 * @param param Operation discriminator.
	 * @return The value passed by notify().
	 */
	size_t wait(const void *key, size_t param);

	/**
	 * @brief Wake ONE thread waiting on (key, param).
	 *
	 * The woken thread enters NOW state for immediate dispatch.
	 *
	 * @param key   Endpoint to match.
	 * @param param Operation to match.
	 * @param value Value returned by the woken thread's wait().
	 * @return true if a thread was woken.
	 */
	static bool notify(const void *key, size_t param, size_t value = 0);

	/**
	 * @brief Wake ALL threads waiting on (key, param).
	 * @param key   Endpoint to match.
	 * @param param Operation to match.
	 * @param value Value returned by each woken thread's wait().
	 * @return Number of threads woken.
	 */
	static size_t notify_all(const void *key, size_t param, size_t value = 0);

	/**
	 * @brief Initialize all threads and run them round-robin.
	 *
	 * For each thread: paints the stack, swaps SP, saves initial
	 * context via bootstrap(), then returns. After all threads are
	 * ready, enters the scheduler loop.
	 *
	 * @param list The thread pool to schedule.
	 */
	static void schedule(LinkList *list);
};

/* =============================================================== */

/**
 * @class Mutex
 * @brief Cooperative mutual exclusion using wait/notify.
 */
class Mutex {
	bool _locked;
public:
	Mutex() : _locked(false) {}

	/// @brief Acquire the lock. Blocks if already locked.
	void lock();

	/// @brief Release the lock. Wakes one waiter.
	void unlock();

	/// @brief True if currently locked.
	bool locked() const { return _locked; }
};

/* =============================================================== */

/**
 * @class Semaphore
 * @brief Counting semaphore using wait/notify.
 */
class Semaphore {
	size_t _count;
	size_t _max;
public:
	/**
	 * @brief Construct a semaphore.
	 * @param initial Initial count.
	 * @param max     Maximum count (default SIZE_MAX).
	 */
	explicit Semaphore(size_t initial = 0, size_t max = (size_t)-1);

	/// @brief Decrement. Blocks if count is zero.
	void acquire();

	/**
	 * @brief Try to decrement without blocking.
	 * @return true if acquired, false if count was zero.
	 */
	bool try_acquire();

	/// @brief Increment and wake one waiter.
	void release();

	/// @brief Current count.
	size_t count() const { return _count; }
};

/* =============================================================== */

/**
 * @class Mailbox
 * @brief Single-slot blocking message passing.
 */
class Mailbox {
	size_t	_msg;
	bool	_full;
public:
	Mailbox() : _msg(0), _full(false) {}

	/**
	 * @brief Send a message. Blocks if the mailbox is full.
	 * @param msg Value to send.
	 */
	void send(size_t msg);

	/**
	 * @brief Receive a message. Blocks if the mailbox is empty.
	 * @return The received value.
	 */
	size_t recv();

	/// @brief True if no message is stored.
	bool empty() const { return !_full; }

	/// @brief True if a message is stored.
	bool full()  const { return _full; }
};

/* =============================================================== */

/**
 * @class Queue
 * @brief Bounded FIFO with blocking push/pop.
 *
 * Backed by a caller-provided buffer (no heap allocation).
 */
class Queue {
	size_t	*_buf;
	size_t	_cap;
	size_t	_head;
	size_t	_tail;
	size_t	_count;
public:
	/**
	 * @brief Construct a queue.
	 * @param buf      Pointer to a size_t array for storage.
	 * @param capacity Number of elements the array can hold.
	 */
	Queue(size_t *buf, size_t capacity);

	/**
	 * @brief Push a value. Blocks if the queue is full.
	 * @param val Value to enqueue.
	 */
	void push(size_t val);

	/**
	 * @brief Pop a value. Blocks if the queue is empty.
	 * @return The dequeued value.
	 */
	size_t pop();

	/// @brief Number of elements currently stored.
	size_t count()    const { return _count; }

	/// @brief Maximum capacity.
	size_t capacity() const { return _cap; }

	/// @brief True if empty.
	bool   empty()    const { return _count == 0; }

	/// @brief True if full.
	bool   full()     const { return _count >= _cap; }
};

#endif /* PARALAX_HPP */
