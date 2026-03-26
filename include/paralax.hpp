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
 * @brief Default stack size when none is specified (bytes).
 *
 * Override before including this header:
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
 * --------------------------------------------------------------- */

/// @brief Return the current time in user-defined units.
extern size_t paralax_getTime();

/// @brief Sleep for the given duration in the same units as paralax_getTime().
extern void paralax_sleepTime(size_t time);

/* =============================================================== */

class LinkList;

/**
 * @class Linkable
 * @brief Intrusive doubly-linked list node.
 */
class Linkable {
public:
	Linkable	*next;
	Linkable	*prev;
	LinkList	*container;

	Linkable() = delete;
	Linkable(LinkList *c);
	virtual ~Linkable();

	void *get() { return (void *)this; }
};

/* =============================================================== */

/**
 * @class LinkList
 * @brief Intrusive doubly-linked list with insertion-sort.
 */
class LinkList {
public:
	Linkable	*head;
	Linkable	*tail;

	LinkList() : head(nullptr), tail(nullptr) {}

	void insert(Linkable *n);
	void remove(Linkable *n);

	Linkable *begin() const { return head; }
	Linkable *end()   const { return nullptr; }

	void sort(bool (*cmp)(Linkable *, Linkable *));
};

/* =============================================================== */

/**
 * @class Thread
 * @brief Cooperative thread with time-based scheduling.
 *
 * Each thread needs a stack buffer. Two options:
 *
 * **1. Framework-allocated** (heap via new[]):
 * @code
 * MyThread t(&list, 4096);         // 4096-byte stack, allocated internally
 * MyThread t(&list);               // uses PARALAX_STACK_SIZE default
 * @endcode
 *
 * **2. User-provided** (static, heap, or any memory region):
 * @code
 * uint8_t buf[1024];
 * MyThread t(&list, buf, sizeof(buf));  // uses your buffer, no allocation
 * @endcode
 *
 * The framework frees internally-allocated stacks on destruction.
 * User-provided buffers are never freed by the framework.
 */
class Thread : public Linkable {
public:
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

	uint8_t		*_stack_ptr;   ///< Active stack buffer.
	size_t		_stack_size;   ///< Size of the active buffer.
	bool		_owns_stack;   ///< True if we allocated _stack_ptr.

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
	bool stack_check() const;

	__attribute__((noinline))
	static void bootstrap();

	static int8_t _tier(const Thread *t);
	static bool   _sched_cmp(Linkable *a, Linkable *b);
	static void   run_loop(LinkList *list);

protected:
	/// @brief Thread entry point — implement in derived classes.
	virtual void run() = 0;

	/// @brief Called when run() returns, before FINISHED. Override for cleanup.
	virtual void finishing() {}

	/// @brief Called on stack overflow detection. Thread is killed after this.
	virtual void on_stack_overflow() {}

public:
	/**
	 * @brief Construct with framework-allocated stack (heap).
	 *
	 * The stack is allocated via new[] and freed on destruction.
	 *
	 * @param list       Thread pool (nullptr to leave unlinked).
	 * @param stack_size Stack size in bytes. Default PARALAX_STACK_SIZE.
	 * @param nice       Min interval between activations. 0 = ASAP.
	 * @param priority   Tiebreaker: lower = higher priority. Default 128.
	 */
	Thread(LinkList *list = nullptr, size_t stack_size = PARALAX_STACK_SIZE,
	       size_t nice = 0, uint8_t priority = 128);

	/**
	 * @brief Construct with a user-provided stack buffer.
	 *
	 * The caller owns the buffer and must keep it alive until the thread
	 * finishes. The framework will never free it.
	 *
	 * @param list      Thread pool (nullptr to leave unlinked).
	 * @param ext_stack Pointer to the external stack buffer.
	 * @param ext_size  Size of the buffer in bytes.
	 * @param nice      Min interval between activations. 0 = ASAP.
	 * @param priority  Tiebreaker: lower = higher priority. Default 128.
	 */
	Thread(LinkList *list, uint8_t *ext_stack, size_t ext_size,
	       size_t nice = 0, uint8_t priority = 128);

	/**
	 * @brief Destructor. Frees the stack if framework-allocated.
	 */
	virtual ~Thread();

	/* --- accessors --- */

	State       state()     const { return _state; }
	size_t      nice()      const { return _nice; }
	uint8_t     priority()  const { return _priority; }
	size_t      next_run()  const { return _next_run; }
	bool        finished()  const { return _state == FINISHED; }
	const void *id()        const { return (const void *)this; }
	size_t      stack_max() const { return _stack_size; }

	static Thread *running() { return _running; }

	/// @brief Human-readable state name.
	const char *state_name() const;

	/// @brief Stack high-water mark (bytes used at peak).
	size_t stack_used() const;

	/// @brief Remaining stack bytes.
	size_t stack_free() const { return _stack_size - stack_used(); }

	/// @brief Voluntarily suspend this thread.
	virtual void yield();

	/**
	 * @brief Suspend until notified on (key, param).
	 * @param key   Endpoint identifier (pointer to any object).
	 * @param param Operation discriminator.
	 * @return The value passed by notify().
	 */
	size_t wait(const void *key, size_t param);

	/**
	 * @brief Wake ONE thread waiting on (key, param).
	 * @return true if a thread was woken.
	 */
	static bool notify(const void *key, size_t param, size_t value = 0);

	/**
	 * @brief Wake ALL threads waiting on (key, param).
	 * @return Number of threads woken.
	 */
	static size_t notify_all(const void *key, size_t param, size_t value = 0);

	/// @brief Initialize all threads and run them round-robin.
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
	void lock();
	void unlock();
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
	explicit Semaphore(size_t initial = 0, size_t max = (size_t)-1);
	void acquire();
	bool try_acquire();
	void release();
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
	void send(size_t msg);
	size_t recv();
	bool empty() const { return !_full; }
	bool full()  const { return _full; }
};

/* =============================================================== */

/**
 * @class Queue
 * @brief Bounded FIFO with blocking push/pop.
 */
class Queue {
	size_t	*_buf;
	size_t	_cap;
	size_t	_head;
	size_t	_tail;
	size_t	_count;
public:
	Queue(size_t *buf, size_t capacity);
	void push(size_t val);
	size_t pop();
	size_t count()    const { return _count; }
	size_t capacity() const { return _cap; }
	bool   empty()    const { return _count == 0; }
	bool   full()     const { return _count >= _cap; }
};

#endif /* PARALAX_HPP */
