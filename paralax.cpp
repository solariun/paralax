class LinkList;

class Linkable {
public:
	Linkable		*next;
	Linkable		*prev;
	LinkList		*container;

public:
	Linkable() = delete;
	Linkable(LinkList *c);
	virtual ~Linkable();

	void *get() { return (void *)this; }
};

class LinkList {
public:
	Linkable	*head;
	Linkable	*tail;

	LinkList() : head(nullptr), tail(nullptr) {}

	void insert(Linkable *n)
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

	void remove(Linkable *n)
	{
		if (n->prev)
			n->prev->next = n->next;
		else
			head = n->next;

		if (n->next)
			n->next->prev = n->prev;
		else
			tail = n->prev;

		n->next      = nullptr;
		n->prev      = nullptr;
		n->container = nullptr;
	}

	Linkable *begin() const { return head; }
	Linkable *end()   const { return nullptr; }

	void sort(bool (*cmp)(Linkable *, Linkable *))
	{
		if (!head || !head->next)
			return;

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
};

inline Linkable::Linkable(LinkList *c)
	: next(nullptr), prev(nullptr), container(c)
{
	if (container)
		container->insert(this);
}

inline Linkable::~Linkable()
{
	if (container)
		container->remove(this);
}



#include <setjmp.h>

class Thread : public Linkable {
public:
	static constexpr unsigned STACK_SIZE = 4096;

private:
	jmp_buf		ctx;
	jmp_buf		caller;
	bool		_finished;

#ifdef THREAD_COPY_STACK
	char		backup[STACK_SIZE];
	char		*stack_base;
	unsigned	stack_used;
	bool		_started;

	static void copy(char *dst, const char *src, unsigned n)
	{
		while (n--) *dst++ = *src++;
	}
#endif

	static jmp_buf	sched_ret;

	static void init_chain(Linkable *node, LinkList *list);
	static void run_loop(LinkList *list);

protected:
	virtual void run() = 0;

public:
	Thread(LinkList *list = nullptr);

	bool finished() const { return _finished; }

	virtual void yield();

	static void schedule(LinkList *list);
};

inline Thread::Thread(LinkList *list)
	: Linkable(list), _finished(false)
#ifdef THREAD_COPY_STACK
	, stack_base(nullptr), stack_used(0), _started(false)
#endif
{}

/* ---------------------------------------------------------------
 * Displacement version (default)
 *
 * schedule() recursively initializes each thread on the real
 * stack using a VLA + computed goto to displace SP. Each init
 * frame stays alive (never returns) so setjmp contexts remain
 * valid for longjmp. The scheduler loop runs at the bottom of
 * the chain.
 * --------------------------------------------------------------- */
#ifndef THREAD_COPY_STACK

inline void Thread::yield()
{
	if (setjmp(ctx) == 0)
		longjmp(caller, 1);
}

inline void Thread::init_chain(Linkable *node, LinkList *list)
{
	Thread *t = (Thread *)node->get();

	volatile char vstack[STACK_SIZE];
	void *entry = &&begin;
	(void)vstack;
	goto *entry;
begin:
	if (setjmp(t->ctx) == 0) {
		Linkable *next = node->next;
		if (next)
			init_chain(next, list);
		else
			longjmp(sched_ret, 1);
		return;
	}

	t->run();
	t->_finished = true;
	longjmp(t->caller, 1);
}

/* ---------------------------------------------------------------
 * Copy version (AVR)
 *
 * All threads share the real stack. On yield the thread's stack
 * frame is copied to a backup buffer; on resume it is restored.
 * No heap execution required.
 * --------------------------------------------------------------- */
#else

inline void Thread::yield()
{
	char anchor;
	stack_used = stack_base - &anchor;
	copy(backup, &anchor, stack_used);

	if (setjmp(ctx) == 0)
		longjmp(caller, 1);
}

inline void Thread::init_chain(Linkable *node, LinkList *list)
{
	Thread *t = (Thread *)node->get();
	t->_started  = false;
	t->stack_used = 0;

	Linkable *next = node->next;
	if (next)
		init_chain(next, list);
	else
		longjmp(sched_ret, 1);
}

#endif /* THREAD_COPY_STACK */

inline void Thread::run_loop(LinkList *list)
{
#ifdef THREAD_COPY_STACK
	char anchor;
#endif

	bool active = true;
	while (active) {
		active = false;
		for (Linkable *n = list->begin(); n != list->end(); n = n->next) {
			Thread *t = (Thread *)n->get();
			if (!t->_finished) {
				active = true;

#ifdef THREAD_COPY_STACK
				t->stack_base = &anchor;
				if (t->stack_used > 0)
					copy(t->stack_base - t->stack_used,
					     t->backup, t->stack_used);

				if (setjmp(t->caller) == 0) {
					if (t->_started) {
						longjmp(t->ctx, 1);
					} else {
						t->_started = true;
						t->run();
						t->_finished = true;
						longjmp(t->caller, 1);
					}
				}
#else
				if (setjmp(t->caller) == 0)
					longjmp(t->ctx, 1);
#endif
			}
		}
	}
}

jmp_buf Thread::sched_ret;

inline void Thread::schedule(LinkList *list)
{
	if (!list->begin())
		return;

	if (setjmp(sched_ret) == 0) {
		init_chain(list->begin(), list);
		return;
	}

	/* scheduler runs here, above all thread stacks */
	run_loop(list);
}