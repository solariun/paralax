#include "paralax.hpp"
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* ---------------------------------------------------------------
 * User-provided time functions (milliseconds)
 * --------------------------------------------------------------- */
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

/* ---------------------------------------------------------------
 * Telemetry
 * --------------------------------------------------------------- */
static void print_threads(LinkList *list)
{
	printf("%-18s %-10s %4s  %6s  %s\n",
		"ID", "STATE", "PRI", "NICE", "STACK USED/MAX (FREE)");
	printf("--------------------------------------------------------------\n");
	for (Linkable *n = list->begin(); n != list->end(); n = n->next) {
		Thread *t = (Thread *)n->get();
		printf("%p  %-10s %3u   %4zu   %5zu/%-5zu (%zu)\n",
			t->id(), t->state_name(), (uint32_t)t->priority(),
			t->nice(), t->stack_used(), t->stack_max(),
			t->stack_free());
	}
}

/* ---------------------------------------------------------------
 * Workers with different intervals
 * --------------------------------------------------------------- */
static size_t start_time;

struct Worker : Thread {
	const char *name;
	int32_t steps;

	Worker(const char *n, int32_t s, LinkList *l, size_t nice, uint8_t pri = 128)
		: Thread(l, PARALAX_STACK_SIZE, nice, pri), name(n), steps(s) {}

	void run() override
	{
		for (int32_t i = 0; i < steps; i++) {
			size_t elapsed = paralax_getTime() - start_time;
			printf("  t=%4zums  [%s] step %d/%d\n", elapsed, name, i + 1, steps);
			yield();
		}
	}
};

/* ---------------------------------------------------------------
 * Semaphore demo — limit concurrency to 1 at a time
 * --------------------------------------------------------------- */
static Semaphore sem(1);
static int32_t shared_resource = 0;

struct SemWorker : Thread {
	const char *name;
	SemWorker(const char *n, LinkList *l) : Thread(l), name(n) {}

	void run() override
	{
		for (int32_t i = 0; i < 3; i++) {
			sem.acquire();
			int32_t val = ++shared_resource;
			printf("  [%s] acquired (resource=%d)\n", name, val);
			yield();
			printf("  [%s] releasing\n", name);
			sem.release();
			yield();
		}
	}
};

/* ---------------------------------------------------------------
 * Main
 * --------------------------------------------------------------- */
int32_t main()
{
	/* --- Time-based scheduling --- */
	{
		printf("=== Time-based scheduling ===\n");
		printf("  Fast:  nice=50ms, Mid: nice=100ms, Slow: nice=200ms pri=10\n\n");

		LinkList threads;
		start_time = paralax_getTime();

		Worker fast("Fast",  6, &threads,  50, 128);
		Worker mid ("Mid",   4, &threads, 100, 128);
		Worker slow("Slow",  3, &threads, 200,  10);

		Thread::schedule(&threads);
		printf("\n");
		print_threads(&threads);
	}

	/* --- Semaphore demo --- */
	{
		printf("\n=== Semaphore (limit=1) ===\n\n");
		LinkList threads;
		shared_resource = 0;

		SemWorker w1("sem-A", &threads);
		SemWorker w2("sem-B", &threads);

		Thread::schedule(&threads);
		printf("\n");
		print_threads(&threads);
	}

	return 0;
}
