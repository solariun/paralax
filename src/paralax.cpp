/**
 * @file paralax.cpp
 * @brief Implementation of the Paralax cooperative threading framework.
 */

#include "paralax.hpp"

/* =================================================================
 * Linkable
 * ================================================================= */

Linkable::Linkable(LinkList *c)
	: next(nullptr), prev(nullptr), container(c)
{
	if (container)
		container->insert(this);
}

Linkable::~Linkable()
{
	if (container)
		container->remove(this);
}

/* =================================================================
 * LinkList
 * ================================================================= */

void LinkList::insert(Linkable *n)
{
	n->next = nullptr;
	n->prev = tail;
	if (tail)
		tail->next = n;
	else
		head = n;
	tail = n;
	n->container = this;
}

void LinkList::remove(Linkable *n)
{
	if (n->prev) n->prev->next = n->next;
	else         head = n->next;
	if (n->next) n->next->prev = n->prev;
	else         tail = n->prev;
	n->next = nullptr;
	n->prev = nullptr;
	n->container = nullptr;
}

void LinkList::sort(bool (*cmp)(Linkable *, Linkable *))
{
	if (!head || !head->next)
		return;

	/* insertion sort — stable, O(n^2) but fine for small thread counts */
	Linkable *sorted = nullptr;
	Linkable *cur    = head;

	while (cur) {
		Linkable *next = cur->next;
		if (!sorted) {
			cur->next = cur->prev = nullptr;
			sorted = cur;
		} else {
			Linkable *s    = sorted;
			Linkable *prev = nullptr;
			while (s && !cmp(cur, s)) {
				prev = s;
				s    = s->next;
			}
			if (!prev) {
				cur->next    = sorted;
				cur->prev    = nullptr;
				sorted->prev = cur;
				sorted       = cur;
			} else {
				cur->next  = prev->next;
				cur->prev  = prev;
				prev->next = cur;
				if (cur->next)
					cur->next->prev = cur;
			}
		}
		cur = next;
	}

	head = sorted;
	tail = sorted;
	while (tail->next)
		tail = tail->next;
}

/* =================================================================
 * Thread — static member storage
 * ================================================================= */

jmp_buf Thread::sched_ret;
Thread *volatile Thread::_init_target;
Thread *volatile Thread::_running;
size_t Thread::_sort_now;

/* =================================================================
 * Thread — constructor
 * ================================================================= */

Thread::Thread(LinkList *list, size_t nice, uint8_t priority)
	: Linkable(list), _state(CREATED), _priority(priority),
	  _nice(nice), _next_run(0),
	  _wait_key(nullptr), _wait_param(0), _wait_value(0)
{}

/* =================================================================
 * Thread — stack watermark
 * ================================================================= */

/* Fill the stack buffer with a known byte pattern. After execution,
 * scanning from the bottom for intact bytes reveals the high-water mark. */
void Thread::stack_paint()
{
	for (size_t i = 0; i < STACK_SIZE; i++)
		stack[i] = STACK_WATERMARK;
}

/* =================================================================
 * Thread — bootstrap (runs on the thread's private stack)
 * ================================================================= */

/* Called after set_sp() moved SP into the thread's buffer.
 * Reads the target from _init_target (static storage, safe after SP change).
 * Saves the initial context with setjmp, then longjmps back to the
 * scheduler. When resumed later, calls run(). */
void Thread::bootstrap()
{
	Thread *t = _init_target;
	if (setjmp(t->ctx) == 0)
		longjmp(sched_ret, 1);

	/* resumed by the scheduler */
	t->_state = RUNNING;
	_running = t;
	t->run();
	t->_state = FINISHED;
	_running = nullptr;
	longjmp(t->caller, 1);
}

/* =================================================================
 * Thread — scheduler internals
 * ================================================================= */

/* Urgency tier: 0=late, 1=NOW, 2=on-time, 3=blocked.
 * Late threads (overdue) get the highest scheduling priority. */
int8_t Thread::_tier(const Thread *t)
{
	if (t->_state == FINISHED || t->_state == WAITING)
		return 3;
	if (t->_state == NOW)
		return 1;
	if (t->_next_run < _sort_now)
		return 0; /* late — overdue */
	return 2;     /* on-time */
}

/* Sort comparator for the scheduler.
 * Tier 0 (late):    most overdue first (smallest next_run = biggest debt).
 * Tier 1 (NOW):     priority only — all need immediate dispatch.
 * Tier 2 (on-time): earliest next_run first.
 * Tier 3 (blocked): sink to the bottom.
 * Within any tier: lower priority value = higher urgency. */
bool Thread::_sched_cmp(Linkable *a, Linkable *b)
{
	Thread *ta = (Thread *)a->get();
	Thread *tb = (Thread *)b->get();

	int8_t ra = _tier(ta);
	int8_t rb = _tier(tb);
	if (ra != rb)
		return ra < rb;

	/* within the same tier, sort by next_run where applicable */
	if (ra == 0 || ra == 2) {
		if (ta->_next_run != tb->_next_run)
			return ta->_next_run < tb->_next_run;
	}

	/* tiebreak: lower priority value wins */
	return ta->_priority < tb->_priority;
}

/* Scheduler loop: sort → pick → sleep-if-early → run → update.
 * Exits when all threads are FINISHED or all are WAITING (deadlock). */
void Thread::run_loop(LinkList *list)
{
	for (;;) {
		/* snapshot time for the comparator */
		_sort_now = paralax_getTime();
		list->sort(_sched_cmp);

		/* find the first runnable thread (list is sorted by urgency) */
		Thread *next = nullptr;
		bool any_alive = false;

		for (Linkable *n = list->begin(); n != list->end(); n = n->next) {
			Thread *t = (Thread *)n->get();
			if (t->_state == FINISHED) continue;
			any_alive = true;
			if (t->_state == WAITING) continue;
			next = t;
			break;
		}

		if (!any_alive) break; /* all finished  */
		if (!next)      break; /* all waiting   */

		/* late and NOW threads run immediately; on-time threads sleep */
		if (next->_state != NOW && next->_next_run > _sort_now)
			paralax_sleepTime(next->_next_run - _sort_now);

		/* context switch into the thread */
		if (setjmp(next->caller) == 0) {
			next->_state = RUNNING;
			_running = next;
			longjmp(next->ctx, 1);
		}

		/* thread yielded, waited, or finished — back in scheduler */
		if (next->_state == RUNNING)
			next->_state = YIELDED;
		_running = nullptr;

		/* schedule next activation */
		next->_next_run = paralax_getTime() + next->_nice;
	}
}

/* =================================================================
 * Thread — public API
 * ================================================================= */

const char *Thread::state_name() const
{
	switch (_state) {
	case CREATED:  return "CREATED";
	case READY:    return "READY";
	case RUNNING:  return "RUNNING";
	case YIELDED:  return "YIELDED";
	case WAITING:  return "WAITING";
	case NOW:      return "NOW";
	case FINISHED: return "FINISHED";
	}
	return "?";
}

size_t Thread::stack_used() const
{
	/* count intact watermark bytes from the bottom (low address) up */
	size_t clean = 0;
	for (size_t i = 0; i < STACK_SIZE; i++) {
		if (stack[i] == STACK_WATERMARK)
			clean++;
		else
			break;
	}
	return STACK_SIZE - clean;
}

void Thread::yield()
{
	_state = YIELDED;
	if (setjmp(ctx) == 0)
		longjmp(caller, 1);
	_state = RUNNING;
}

size_t Thread::wait(const void *key, size_t param)
{
	_wait_key   = key;
	_wait_param = param;
	_wait_value = 0;
	_state = WAITING;

	if (setjmp(ctx) == 0)
		longjmp(caller, 1);

	/* resumed — notify() set _wait_value and state=NOW */
	_state = RUNNING;
	return _wait_value;
}

bool Thread::notify(const void *key, size_t param, size_t value)
{
	if (!_running || !_running->container)
		return false;

	LinkList *list = _running->container;
	for (Linkable *n = list->begin(); n != list->end(); n = n->next) {
		Thread *t = (Thread *)n->get();
		if (t->_state == WAITING &&
		    t->_wait_key == key &&
		    t->_wait_param == param) {
			t->_wait_value = value;
			t->_state = NOW;
			return true;
		}
	}
	return false;
}

size_t Thread::notify_all(const void *key, size_t param, size_t value)
{
	if (!_running || !_running->container)
		return 0;

	size_t count = 0;
	LinkList *list = _running->container;
	for (Linkable *n = list->begin(); n != list->end(); n = n->next) {
		Thread *t = (Thread *)n->get();
		if (t->_state == WAITING &&
		    t->_wait_key == key &&
		    t->_wait_param == param) {
			t->_wait_value = value;
			t->_state = NOW;
			count++;
		}
	}
	return count;
}

void Thread::schedule(LinkList *list)
{
	size_t now = paralax_getTime();

	/* Phase 1: initialize each thread's private stack.
	 * For each thread: paint the stack, save scheduler context,
	 * move SP into the stack buffer, run bootstrap() which saves
	 * the thread's initial context and longjmps back. */
	for (Linkable *n = list->begin(); n != list->end(); n = n->next) {
		Thread *t = (Thread *)n->get();
		t->stack_paint();
		t->_state    = READY;
		t->_next_run = now;

		_init_target = t;
		if (setjmp(sched_ret) == 0) {
			uintptr_t top = (uintptr_t)(t->stack + STACK_SIZE);
			top = (top - 16) & ~(uintptr_t)0xF;
			set_sp((void *)top);
			bootstrap();
		}
	}

	/* Phase 2: run the scheduler loop until all threads finish */
	run_loop(list);
}

/* =================================================================
 * Mutex
 * ================================================================= */

void Mutex::lock()
{
	while (_locked)
		Thread::running()->wait(this, 0);
	_locked = true;
}

void Mutex::unlock()
{
	_locked = false;
	Thread::notify(this, 0);
}

/* =================================================================
 * Semaphore
 * ================================================================= */

Semaphore::Semaphore(size_t initial, size_t max)
	: _count(initial), _max(max)
{}

void Semaphore::acquire()
{
	while (_count == 0)
		Thread::running()->wait(this, 0);
	--_count;
}

bool Semaphore::try_acquire()
{
	if (_count == 0)
		return false;
	--_count;
	return true;
}

void Semaphore::release()
{
	if (_count < _max)
		++_count;
	Thread::notify(this, 0);
}

/* =================================================================
 * Mailbox
 * ================================================================= */

void Mailbox::send(size_t msg)
{
	while (_full)
		Thread::running()->wait(this, 1);
	_msg  = msg;
	_full = true;
	Thread::notify(this, 0, msg);
}

size_t Mailbox::recv()
{
	while (!_full)
		Thread::running()->wait(this, 0);
	_full = false;
	size_t msg = _msg;
	Thread::notify(this, 1);
	return msg;
}

/* =================================================================
 * Queue
 * ================================================================= */

Queue::Queue(size_t *buf, size_t capacity)
	: _buf(buf), _cap(capacity), _head(0), _tail(0), _count(0)
{}

void Queue::push(size_t val)
{
	while (_count >= _cap)
		Thread::running()->wait(this, 1);
	_buf[_tail] = val;
	_tail = (_tail + 1) % _cap;
	_count++;
	Thread::notify(this, 0, val);
}

size_t Queue::pop()
{
	while (_count == 0)
		Thread::running()->wait(this, 0);
	size_t val = _buf[_head];
	_head = (_head + 1) % _cap;
	_count--;
	Thread::notify(this, 1);
	return val;
}
