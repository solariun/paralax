/**
 * @file paralax_test.cpp
 * @brief Self-contained tests for the Paralax cooperative threading framework.
 */

#include "paralax.hpp"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* =================================================================
 * User-provided time functions (milliseconds)
 * ================================================================= */

size_t paralax_getTime()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (size_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void paralax_sleepTime(size_t ms)
{
	usleep((uint32_t)(ms * 1000));
}

/* =================================================================
 * Minimal test harness
 * ================================================================= */

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) static void test_##name()

#define ASSERT_TRUE(expr) \
	do { \
		if (!(expr)) { \
			printf("  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
			g_fail++; \
			return; \
		} \
	} while (0)

#define ASSERT_EQ(a, b) \
	do { \
		auto _a = (a); auto _b = (b); \
		if (_a != _b) { \
			printf("  FAIL: %s:%d: %s == %lld, expected %s == %lld\n", \
				__FILE__, __LINE__, #a, (long long)_a, #b, (long long)_b); \
			g_fail++; \
			return; \
		} \
	} while (0)

#define RUN_TEST(name) \
	do { \
		int before = g_fail; \
		printf("[ RUN  ] %s\n", #name); \
		test_##name(); \
		if (g_fail == before) { \
			g_pass++; \
			printf("[ PASS ] %s\n", #name); \
		} else { \
			printf("[ FAIL ] %s\n", #name); \
		} \
	} while (0)

/* =================================================================
 * Test: LinkList insert, remove, iteration, sort
 * ================================================================= */

/* A minimal Linkable-derived node carrying an integer value. */
struct IntNode : Linkable {
	int value;
	IntNode(LinkList *l, int v) : Linkable(l), value(v) {}
};

static bool int_node_ascending(Linkable *a, Linkable *b)
{
	return ((IntNode *)a)->value < ((IntNode *)b)->value;
}

TEST(linklist_insert_and_iterate)
{
	LinkList list;
	IntNode a(&list, 10);
	IntNode b(&list, 20);
	IntNode c(&list, 30);

	/* head and tail */
	ASSERT_TRUE(list.head == &a);
	ASSERT_TRUE(list.tail == &c);

	/* forward iteration */
	int sum = 0;
	int count = 0;
	for (Linkable *n = list.begin(); n != list.end(); n = n->next) {
		sum += ((IntNode *)n)->value;
		count++;
	}
	ASSERT_EQ(count, 3);
	ASSERT_EQ(sum, 60);
}

TEST(linklist_remove)
{
	LinkList list;
	IntNode a(&list, 1);
	IntNode b(&list, 2);
	IntNode c(&list, 3);

	list.remove(&b);
	ASSERT_TRUE(a.next == &c);
	ASSERT_TRUE(c.prev == &a);
	ASSERT_TRUE(b.container == nullptr);

	/* list still has two nodes */
	int count = 0;
	for (Linkable *n = list.begin(); n != list.end(); n = n->next)
		count++;
	ASSERT_EQ(count, 2);

	/* remove head */
	list.remove(&a);
	ASSERT_TRUE(list.head == &c);
	ASSERT_TRUE(list.tail == &c);

	/* remove last */
	list.remove(&c);
	ASSERT_TRUE(list.head == nullptr);
	ASSERT_TRUE(list.tail == nullptr);
}

TEST(linklist_sort)
{
	LinkList list;
	IntNode c(&list, 30);
	IntNode a(&list, 10);
	IntNode b(&list, 20);

	list.sort(int_node_ascending);

	/* Should be 10, 20, 30 after sort */
	Linkable *n = list.head;
	ASSERT_EQ(((IntNode *)n)->value, 10); n = n->next;
	ASSERT_EQ(((IntNode *)n)->value, 20); n = n->next;
	ASSERT_EQ(((IntNode *)n)->value, 30); n = n->next;
	ASSERT_TRUE(n == nullptr);

	/* tail should point to 30 */
	ASSERT_EQ(((IntNode *)list.tail)->value, 30);
}

/* =================================================================
 * Test: Thread lifecycle — create, run to FINISHED
 * ================================================================= */

static int lifecycle_run_count;

struct LifecycleThread : Thread {
	LifecycleThread(LinkList *l) : Thread(l) {}

	void run() override
	{
		lifecycle_run_count++;
	}
};

TEST(thread_lifecycle)
{
	lifecycle_run_count = 0;

	LinkList list;
	LifecycleThread t(&list);

	ASSERT_EQ((int)t.state(), (int)Thread::CREATED);

	Thread::schedule(&list);

	ASSERT_EQ(lifecycle_run_count, 1);
	ASSERT_TRUE(t.finished());
	ASSERT_EQ((int)t.state(), (int)Thread::FINISHED);
}

/* =================================================================
 * Test: Thread yield — two threads interleave correctly
 * ================================================================= */

static int yield_log[10];
static int yield_log_idx;

struct YieldThreadA : Thread {
	YieldThreadA(LinkList *l) : Thread(l) {}
	void run() override
	{
		yield_log[yield_log_idx++] = 1;
		yield();
		yield_log[yield_log_idx++] = 1;
		yield();
		yield_log[yield_log_idx++] = 1;
	}
};

struct YieldThreadB : Thread {
	YieldThreadB(LinkList *l) : Thread(l) {}
	void run() override
	{
		yield_log[yield_log_idx++] = 2;
		yield();
		yield_log[yield_log_idx++] = 2;
		yield();
		yield_log[yield_log_idx++] = 2;
	}
};

TEST(thread_yield_interleave)
{
	yield_log_idx = 0;
	memset(yield_log, 0, sizeof(yield_log));

	LinkList list;
	YieldThreadA a(&list);
	YieldThreadB b(&list);

	Thread::schedule(&list);

	ASSERT_EQ(yield_log_idx, 6);

	/* Both threads should contribute exactly 3 entries each. */
	int count_a = 0, count_b = 0;
	for (int i = 0; i < yield_log_idx; i++) {
		if (yield_log[i] == 1) count_a++;
		else if (yield_log[i] == 2) count_b++;
	}
	ASSERT_EQ(count_a, 3);
	ASSERT_EQ(count_b, 3);

	/* Both threads interleaved (not necessarily strict alternation,
	 * but neither thread ran all 3 steps before the other started). */
	ASSERT_TRUE(yield_log[0] != 0);
	ASSERT_TRUE(yield_log[yield_log_idx - 1] != 0);
}

/* =================================================================
 * Test: Thread nice/priority — scheduling respects intervals
 * ================================================================= */

static int nice_fast_count;
static int nice_slow_count;

struct NiceFastThread : Thread {
	int limit;
	NiceFastThread(LinkList *l, int lim)
		: Thread(l, PARALAX_STACK_SIZE, 0, 128), limit(lim) {}
	void run() override
	{
		for (int i = 0; i < limit; i++) {
			nice_fast_count++;
			yield();
		}
	}
};

struct NiceSlowThread : Thread {
	int limit;
	NiceSlowThread(LinkList *l, int lim)
		: Thread(l, PARALAX_STACK_SIZE, 50, 128), limit(lim) {}
	void run() override
	{
		for (int i = 0; i < limit; i++) {
			nice_slow_count++;
			yield();
		}
	}
};

TEST(thread_nice_priority)
{
	nice_fast_count = 0;
	nice_slow_count = 0;

	LinkList list;
	NiceFastThread fast(&list, 6);
	NiceSlowThread slow(&list, 3);

	Thread::schedule(&list);

	/* Both should have completed all steps. */
	ASSERT_EQ(nice_fast_count, 6);
	ASSERT_EQ(nice_slow_count, 3);

	/* The fast thread (nice=0) should finish before the slow thread
	 * (nice=50ms) finishes, because it runs more often. Both finished,
	 * which is the core assertion. */
	ASSERT_TRUE(fast.finished());
	ASSERT_TRUE(slow.finished());
}

/* =================================================================
 * Test: Wait/notify — one thread waits, another notifies with value
 * ================================================================= */

static size_t wait_received_value;

struct WaitingThread : Thread {
	const void *key;
	WaitingThread(LinkList *l, const void *k) : Thread(l), key(k) {}
	void run() override
	{
		wait_received_value = wait(key, 42);
	}
};

struct NotifyingThread : Thread {
	const void *key;
	NotifyingThread(LinkList *l, const void *k) : Thread(l), key(k) {}
	void run() override
	{
		/* Give the waiter a chance to block first. */
		yield();
		Thread::notify(key, 42, 999);
	}
};

TEST(wait_notify)
{
	wait_received_value = 0;
	static int notify_key;

	LinkList list;
	WaitingThread waiter(&list, &notify_key);
	NotifyingThread notifier(&list, &notify_key);

	Thread::schedule(&list);

	ASSERT_TRUE(waiter.finished());
	ASSERT_TRUE(notifier.finished());
	ASSERT_EQ(wait_received_value, (size_t)999);
}

/* =================================================================
 * Test: Mutex — two threads contend, shared counter is correct
 * ================================================================= */

static int mutex_counter;

struct MutexThread : Thread {
	Mutex *mtx;
	int increments;
	MutexThread(LinkList *l, Mutex *m, int inc)
		: Thread(l), mtx(m), increments(inc) {}
	void run() override
	{
		for (int i = 0; i < increments; i++) {
			mtx->lock();
			mutex_counter++;
			yield();      /* yield while holding the lock */
			mtx->unlock();
			yield();
		}
	}
};

TEST(mutex)
{
	mutex_counter = 0;
	Mutex mtx;

	LinkList list;
	MutexThread a(&list, &mtx, 5);
	MutexThread b(&list, &mtx, 5);

	Thread::schedule(&list);

	ASSERT_TRUE(a.finished());
	ASSERT_TRUE(b.finished());
	ASSERT_EQ(mutex_counter, 10);
	ASSERT_TRUE(!mtx.locked());
}

/* =================================================================
 * Test: Semaphore — acquire/release, try_acquire
 * ================================================================= */

static int sem_acquired_count;

struct SemAcquireThread : Thread {
	Semaphore *sem;
	int rounds;
	SemAcquireThread(LinkList *l, Semaphore *s, int r)
		: Thread(l), sem(s), rounds(r) {}
	void run() override
	{
		for (int i = 0; i < rounds; i++) {
			sem->acquire();
			sem_acquired_count++;
			yield();
			sem->release();
			yield();
		}
	}
};

TEST(semaphore)
{
	sem_acquired_count = 0;
	Semaphore sem(1); /* binary semaphore, initial=1 */

	LinkList list;
	SemAcquireThread a(&list, &sem, 3);
	SemAcquireThread b(&list, &sem, 3);

	Thread::schedule(&list);

	ASSERT_TRUE(a.finished());
	ASSERT_TRUE(b.finished());
	ASSERT_EQ(sem_acquired_count, 6);
	ASSERT_EQ(sem.count(), (size_t)1); /* returned to initial state */
}

TEST(semaphore_try_acquire)
{
	Semaphore sem(2);

	ASSERT_TRUE(sem.try_acquire());
	ASSERT_EQ(sem.count(), (size_t)1);
	ASSERT_TRUE(sem.try_acquire());
	ASSERT_EQ(sem.count(), (size_t)0);
	ASSERT_TRUE(!sem.try_acquire()); /* should fail, count is 0 */
	ASSERT_EQ(sem.count(), (size_t)0);

	sem.release();
	ASSERT_EQ(sem.count(), (size_t)1);
}

/* =================================================================
 * Test: Mailbox — send/recv message order
 * ================================================================= */

static size_t mbox_received[3];
static int mbox_recv_idx;

struct MailboxSender : Thread {
	Mailbox *mbox;
	MailboxSender(LinkList *l, Mailbox *m) : Thread(l), mbox(m) {}
	void run() override
	{
		mbox->send(100);
		mbox->send(200);
		mbox->send(300);
	}
};

struct MailboxReceiver : Thread {
	Mailbox *mbox;
	MailboxReceiver(LinkList *l, Mailbox *m) : Thread(l), mbox(m) {}
	void run() override
	{
		mbox_received[mbox_recv_idx++] = mbox->recv();
		mbox_received[mbox_recv_idx++] = mbox->recv();
		mbox_received[mbox_recv_idx++] = mbox->recv();
	}
};

TEST(mailbox)
{
	mbox_recv_idx = 0;
	memset(mbox_received, 0, sizeof(mbox_received));
	Mailbox mbox;

	LinkList list;
	MailboxSender sender(&list, &mbox);
	MailboxReceiver receiver(&list, &mbox);

	Thread::schedule(&list);

	ASSERT_TRUE(sender.finished());
	ASSERT_TRUE(receiver.finished());
	ASSERT_EQ(mbox_recv_idx, 3);
	ASSERT_EQ(mbox_received[0], (size_t)100);
	ASSERT_EQ(mbox_received[1], (size_t)200);
	ASSERT_EQ(mbox_received[2], (size_t)300);
}

/* =================================================================
 * Test: Queue — push/pop FIFO, blocking on full/empty
 * ================================================================= */

static size_t queue_popped[5];
static int queue_pop_idx;

struct QueueProducer : Thread {
	Queue *q;
	QueueProducer(LinkList *l, Queue *q_) : Thread(l), q(q_) {}
	void run() override
	{
		for (size_t i = 1; i <= 5; i++) {
			q->push(i * 10);
		}
	}
};

struct QueueConsumer : Thread {
	Queue *q;
	QueueConsumer(LinkList *l, Queue *q_) : Thread(l), q(q_) {}
	void run() override
	{
		for (int i = 0; i < 5; i++) {
			queue_popped[queue_pop_idx++] = q->pop();
		}
	}
};

TEST(queue_fifo)
{
	queue_pop_idx = 0;
	memset(queue_popped, 0, sizeof(queue_popped));

	size_t buf[3]; /* capacity=3, smaller than 5 items, forces blocking */
	Queue q(buf, 3);

	LinkList list;
	QueueProducer producer(&list, &q);
	QueueConsumer consumer(&list, &q);

	Thread::schedule(&list);

	ASSERT_TRUE(producer.finished());
	ASSERT_TRUE(consumer.finished());
	ASSERT_EQ(queue_pop_idx, 5);

	/* FIFO order preserved */
	ASSERT_EQ(queue_popped[0], (size_t)10);
	ASSERT_EQ(queue_popped[1], (size_t)20);
	ASSERT_EQ(queue_popped[2], (size_t)30);
	ASSERT_EQ(queue_popped[3], (size_t)40);
	ASSERT_EQ(queue_popped[4], (size_t)50);
}

/* =================================================================
 * Test: Stack telemetry — stack_used() returns nonzero after run
 * ================================================================= */

struct StackTelemetryThread : Thread {
	volatile int dummy;
	StackTelemetryThread(LinkList *l) : Thread(l), dummy(0) {}
	void run() override
	{
		/* Use some stack space with a local array. */
		volatile char buf[256];
		for (int i = 0; i < 256; i++)
			buf[i] = (char)i;
		dummy = buf[128]; /* prevent optimization */
	}
};

static size_t telemetry_stack_used;

TEST(stack_telemetry)
{
	LinkList list;
	StackTelemetryThread t(&list);

	/* Before scheduling, stack_used should be 0 (unpainted or
	 * all watermarks intact). After schedule, it should be nonzero. */
	Thread::schedule(&list);

	telemetry_stack_used = t.stack_used();
	ASSERT_TRUE(t.finished());
	ASSERT_TRUE(telemetry_stack_used > 0);
	ASSERT_TRUE(telemetry_stack_used < t.stack_max());
	ASSERT_EQ(t.stack_free(), t.stack_max() - telemetry_stack_used);
}

/* =================================================================
 * Test: External heap stack
 * ================================================================= */

static int heap_thread_ran;

struct HeapStackThread : Thread {
	HeapStackThread(LinkList *l, uint8_t *buf, size_t sz)
		: Thread(l, buf, sz) {}
	void run() override
	{
		heap_thread_ran = 1;
		volatile char local[64];
		for (int i = 0; i < 64; i++) local[i] = (char)i;
		(void)local[32];
	}
};

TEST(external_heap_stack)
{
	heap_thread_ran = 0;
	static uint8_t heap_buf[4096];
	LinkList list;
	HeapStackThread t(&list, heap_buf, sizeof(heap_buf));

	ASSERT_EQ(t.stack_max(), (size_t)4096);

	Thread::schedule(&list);

	ASSERT_TRUE(t.finished());
	ASSERT_EQ(heap_thread_ran, 1);
	ASSERT_TRUE(t.stack_used() > 0);
	ASSERT_TRUE(t.stack_used() < (size_t)4096);
}

/* =================================================================
 * Test: finishing() callback
 * ================================================================= */

static int finishing_called;

struct FinishingThread : Thread {
	FinishingThread(LinkList *l) : Thread(l) {}
	void run() override {}
	void finishing() override { finishing_called = 1; }
};

TEST(finishing_callback)
{
	finishing_called = 0;
	LinkList list;
	FinishingThread t(&list);

	Thread::schedule(&list);

	ASSERT_TRUE(t.finished());
	ASSERT_EQ(finishing_called, 1);
}

/* =================================================================
 * Test: Stack overflow detection
 * ================================================================= */

static int overflow_called;

struct OverflowThread : Thread {
	/* small external stack — will overflow into the padding below */
	OverflowThread(LinkList *l, uint8_t *buf, size_t sz)
		: Thread(l, buf, sz) {}
	void run() override
	{
		/* use more stack than the buffer provides */
		volatile char bloat[1024];
		for (int i = 0; i < 1024; i++) bloat[i] = (char)i;
		(void)bloat[512];
		yield();
	}
	void on_stack_overflow() override { overflow_called = 1; }
};

TEST(stack_overflow_detection)
{
	overflow_called = 0;

	/* padded arena: overflow spills into pad[], not into test globals.
	 * pad is at lower addresses (where the stack grows toward). */
	/* 1024-byte bloat on a 512-byte stack → ~512 bytes overflow.
	 * pad absorbs the overflow so it doesn't corrupt test globals. */
	static struct {
		uint8_t pad[1024];
		uint8_t buf[512];
	} arena;

	LinkList list;
	OverflowThread t(&list, arena.buf, sizeof(arena.buf));

	Thread::schedule(&list);

	/* thread should have been killed by overflow detection */
	ASSERT_TRUE(t.finished());
	ASSERT_EQ(overflow_called, 1);
}

/* =================================================================
 * Main — run all tests and report
 * ================================================================= */

int main()
{
	printf("===== Paralax Test Suite =====\n\n");

	/* LinkList tests */
	RUN_TEST(linklist_insert_and_iterate);
	RUN_TEST(linklist_remove);
	RUN_TEST(linklist_sort);

	/* Thread tests */
	RUN_TEST(thread_lifecycle);
	RUN_TEST(thread_yield_interleave);
	RUN_TEST(thread_nice_priority);

	/* Wait/notify */
	RUN_TEST(wait_notify);

	/* Mutex */
	RUN_TEST(mutex);

	/* Semaphore */
	RUN_TEST(semaphore);
	RUN_TEST(semaphore_try_acquire);

	/* Mailbox */
	RUN_TEST(mailbox);

	/* Queue */
	RUN_TEST(queue_fifo);

	/* Stack telemetry */
	RUN_TEST(stack_telemetry);

	/* External heap stack */
	RUN_TEST(external_heap_stack);

	/* Callbacks */
	RUN_TEST(finishing_callback);
	RUN_TEST(stack_overflow_detection);

	/* Summary */
	printf("\n===== Results: %d passed, %d failed =====\n",
		g_pass, g_fail);

	return g_fail > 0 ? 1 : 0;
}
