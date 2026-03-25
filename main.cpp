#include "paralax.cpp"
#include <stdio.h>

struct Worker : Thread {
	const char	*name;
	int		steps;

	Worker(const char *n, int s, LinkList *list)
		: Thread(list), name(n), steps(s) {}

	void run() override
	{
		for (int i = 0; i < steps; i++) {
			printf("[%s] step %d/%d\n", name, i + 1, steps);
			yield();
		}
	}
};

int main()
{
	LinkList threads;

	Worker w1("Alpha", 3, &threads);
	Worker w2("Beta",  2, &threads);
	Worker w3("Gamma", 4, &threads);

	printf("Cooperative threading demo:\n\n");
	Thread::schedule(&threads);
	printf("\nAll threads finished.\n");

	return 0;
}
