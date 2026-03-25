# Paralax

A portable cooperative threading framework in pure C++ with no external dependencies and no assembly.

## Features

- **Intrusive linked list** (`Linkable` / `LinkList`) with sort support
- **Cooperative threads** (`Thread`) using `setjmp`/`longjmp` and computed goto (GCC/Clang)
- **Two build-time strategies:**
  - **Stack displacement** (default) — each thread gets its own virtual stack via VLA + computed goto
  - **Stack copy** (`-DTHREAD_COPY_STACK`) — threads share the real stack, frames are saved/restored via memcpy (suitable for AVR and constrained targets)
- No assembly, no external libraries, no platform-specific code

## Building

```bash
# Default (stack displacement)
c++ -o paralax main.cpp

# AVR / stack copy mode
c++ -DTHREAD_COPY_STACK -o paralax main.cpp
```

## Usage

Inherit from `Thread`, implement `run()`, call `yield()` to cooperate:

```cpp
#include "paralax.cpp"

struct MyThread : Thread {
    int id;

    MyThread(int id, LinkList *list)
        : Thread(list), id(id) {}

    void run() override {
        for (int i = 0; i < 3; i++) {
            printf("[%d] step %d\n", id, i);
            yield();
        }
    }
};

int main() {
    LinkList threads;
    MyThread t1(1, &threads);
    MyThread t2(2, &threads);
    Thread::schedule(&threads);
}
```

Output:
```
[1] step 0
[2] step 0
[1] step 1
[2] step 1
[1] step 2
[2] step 2
```

## How it works

### Stack displacement (default)

`schedule()` recursively initializes each thread's stack via a VLA that displaces the stack pointer. Computed goto (`&&label` / `goto *ptr`) prevents the compiler from optimizing the displacement away. `setjmp` saves each thread's context; `longjmp` switches between them. The scheduler runs above all thread stacks so thread execution never corrupts it.

### Stack copy (AVR)

All threads share the real stack. On `yield()`, the thread's stack frame is copied to a per-thread backup buffer. On resume, the backup is restored before `longjmp`-ing back. No heap execution required.

## Requirements

- GCC or Clang (computed goto extension: `&&label` / `goto *ptr`)
- C++11 or later

## License

[MIT](LICENSE)
