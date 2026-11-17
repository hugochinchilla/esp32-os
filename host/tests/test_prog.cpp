// Programs on tasks of their own (backlog item 26): the runner by itself.
// The "UI task" is the test's main thread; program tasks are real threads.
// (Culebra played on a task of its own is in test_snake.cpp.)
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "core/prog/prog.hpp"
#include "../host_adapters.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

// What a program that takes too long does, step by step, as the test lets it.
struct Slow {
    host::ThreadEvent *gate;    // it sleeps here: "hung"
    std::atomic<int> *touched;  // counts its OS calls that got in
    prog::Runner *runner;
    int in, out;
    bool calls = true;
    void operator()()
    {
        out = in + 1;
        gate->wait(0xFFFFFFFF);
        if (!calls) {
            return;
        }
        const prog::InCall os_call; // as every OS call begins
        ++*touched;
    }
};

struct News {
    int stopped = 0, back = 0;
    proc::Pid last = proc::kNone;
    static void told(proc::Pid pid, bool responding, void *context)
    {
        auto *n = static_cast<News *>(context);
        (responding ? n->back : n->stopped)++;
        n->last = pid;
    }
};

template <class F> bool soon(F &&f)
{
    for (int i = 0; i < 400 && !f(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return f();
}

// The limit on the wait: a program that does not come back.
void test_late()
{
    using proc::Pid;
    const std::thread::id ui = std::this_thread::get_id();
    proc::Table procs;
    const Pid root = procs.spawn("desktop", proc::kNone);
    const Pid a = procs.spawn("a", root, proc::kWindowed);
    prog::Runner runner(procs);
    host::ThreadTasks tasks;
    host::ThreadEvent events[2], gate;
    prog::Kit kits[1] = {{&events[0], &events[1]}};
    runner.set_tasks(&tasks, kits, 1);
    News news;
    runner.set_news(&News::told, &news);
    std::atomic<int> touched{0};
    CHECK(runner.start(a));

    // In time: what the callable keeps in itself comes back, and its OS
    // calls go straight in.
    runner.set_limit(2000);
    {
        gate.set();
        Slow slow{&gate, &touched, &runner, 41, 0};
        CHECK(proc::call_as(&procs, a, slow));
        CHECK(slow.out == 42 && touched == 1 && !runner.late(a) && news.stopped == 0);
    }

    // Too long: the caller goes on without it, as itself again.
    runner.set_limit(30);
    {
        Slow slow{&gate, &touched, &runner, 1, 0};
        CHECK(!proc::call_as(&procs, a, slow));
        CHECK(slow.out == 0); // nothing is copied back from a call that did not finish
        CHECK(runner.late(a) && news.stopped == 1 && news.last == a && procs.current() == proc::kNone);
        CHECK(tasks.last_priority == ports::Priority::Idle); // it has the time nobody else wants
        CHECK(!runner.waiting());
    }
    // While it is late no call is made for it: not there, and not here.
    {
        bool made = false;
        bool *flag = &made;
        CHECK(!proc::call_as(&procs, a, [flag] { *flag = true; }));
        CHECK(!made);
    }
    // It wakes up and wants an OS call. The desktop has gone on, so it is
    // kept out until the UI task has nothing else under way.
    gate.set();
    CHECK(soon([&] { return runner.waiting(); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(touched == 1 && runner.late(a));
    runner.serve();
    CHECK(touched == 2);
    // And then it has finished: taken back, and called again as before.
    CHECK(soon([&] {
        runner.serve();
        return !runner.late(a);
    }));
    CHECK(news.back == 1 && !runner.waiting() && procs.current() == proc::kNone);
    CHECK(tasks.last_priority == ports::Priority::High);
    {
        runner.set_limit(2000);
        gate.set();
        Slow slow{&gate, &touched, &runner, 7, 0};
        CHECK(proc::call_as(&procs, a, slow) && slow.out == 8 && touched == 3);
        std::thread::id on;
        std::thread::id *where = &on;
        CHECK(proc::call_as(&procs, a, [where] { *where = std::this_thread::get_id(); }) && on != ui);
    }

    // Ended while it hangs. A thread of the PC cannot be taken away, so its
    // task stays until it returns by itself; the process is gone at once.
    runner.set_limit(30);
    {
        Slow slow{&gate, &touched, &runner, 1, 0, false};
        CHECK(!proc::call_as(&procs, a, slow) && runner.late(a));
        CHECK(procs.exit(a));
        CHECK(!procs.alive(a) && !runner.has_task(a) && runner.tasks() == 1);
        gate.set();
        CHECK(soon([&] {
            runner.serve();
            return runner.tasks() == 0;
        }));
        CHECK(news.back == 1); // nobody is told it is back
    }
    // The kit is free again.
    const Pid b = procs.spawn("b", root, proc::kWindowed);
    runner.set_limit(2000);
    CHECK(runner.start(b));
    int n = 0;
    int *count = &n;
    CHECK(proc::call_as(&procs, b, [count] { ++*count; }) && n == 1);
    CHECK(procs.exit(b) && runner.tasks() == 0);
}

// The queue of events (backlog items 26 and 37): what happens to a program
// while it is not responding waits for it, and reaches it in order when it
// answers again.
struct Note {
    char *out; // what the program has been handed, one letter a call
    char letter;
    void operator()()
    {
        size_t n = strlen(out);
        out[n] = letter;
        out[n + 1] = 0;
    }
};

// A call that hangs until the gate opens, then leaves its letter.
struct Hang {
    host::ThreadEvent *gate;
    char *out;
    char letter;
    void operator()()
    {
        gate->wait(0xFFFFFFFF);
        Note{out, letter}();
    }
};

void test_queue()
{
    using proc::Pid;
    proc::Table procs;
    const Pid root = procs.spawn("desktop", proc::kNone);
    const Pid a = procs.spawn("a", root, proc::kWindowed);
    prog::Runner runner(procs);
    host::ThreadTasks tasks;
    host::ThreadEvent events[2], gate;
    host::HostMemory memory;
    prog::Kit kits[1] = {{&events[0], &events[1]}};
    runner.set_tasks(&tasks, kits, 1);
    runner.set_memory(&memory);
    News news;
    runner.set_news(&News::told, &news);
    CHECK(runner.start(a));
    const uint32_t stack = procs.find(a)->memory;
    static char got[64];
    got[0] = 0;
    const auto back = [&] {
        return soon([&] {
            runner.serve();
            return !runner.late(a);
        });
    };

    // A program that answers gets its calls at once: nothing waits.
    runner.set_limit(2000);
    CHECK(proc::call_as(&procs, a, Note{got, 'a'}, proc::kKept) && runner.kept(a) == 0 && memory.in_use() == 0);

    // It hangs. What is to be kept waits, in memory counted as its own;
    // what is not is dropped; of what only the last matters, the last
    // stays, in the place of the first.
    runner.set_limit(30);
    CHECK(!proc::call_as(&procs, a, Hang{&gate, got, 'h'}, proc::kKept) && runner.late(a));
    CHECK(!proc::call_as(&procs, a, Note{got, 'b'}, proc::kKept));
    CHECK(!proc::call_as(&procs, a, Note{got, 'x'})); // dropped
    CHECK(!proc::call_as(&procs, a, Note{got, 'm'}, proc::latest(7)));
    CHECK(!proc::call_as(&procs, a, Note{got, 'n'}, proc::latest(7)));
    CHECK(!proc::call_as(&procs, a, Note{got, 'c'}, proc::kKept));
    CHECK(!proc::call_as(&procs, a, Note{got, 'o'}, proc::latest(7))); // not the newest waiting: its own place
    CHECK(!proc::call_as(&procs, a, Note{got, 'p'}, proc::latest(8))); // another kind
    {
        // By pointer: it cannot be kept.
        bool made = false;
        CHECK(!procs.call(a, [](void *flag) { *static_cast<bool *>(flag) = true; }, &made, 0, proc::kKept) && !made);
    }
    CHECK(runner.kept(a) == 5 && memory.in_use() == 5 && procs.find(a)->memory > stack);
    CHECK(strcmp(got, "a") == 0);

    // It answers again: all of it, in order, before anything new, and the
    // memory goes back.
    gate.set();
    CHECK(back());
    CHECK(strcmp(got, "ahbncop") == 0);
    CHECK(runner.kept(a) == 0 && memory.in_use() == 0 && procs.find(a)->memory == stack && news.back == 1);
    runner.set_limit(2000);
    gate.set();
    CHECK(proc::call_as(&procs, a, Note{got, 'd'}, proc::kKept) && strcmp(got, "ahbncopd") == 0);

    // Late again in the middle of what waited: the rest goes on waiting,
    // and what comes now goes behind it.
    got[0] = 0;
    gate.wait(0); // closed
    runner.set_limit(30);
    CHECK(!proc::call_as(&procs, a, Hang{&gate, got, '1'}, proc::kKept));
    CHECK(!proc::call_as(&procs, a, Hang{&gate, got, '2'}, proc::kKept));
    CHECK(!proc::call_as(&procs, a, Note{got, '3'}, proc::kKept));
    gate.set(); // lets the first go; the second closes the gate behind it and hangs
    CHECK(soon([&] {
        runner.serve();
        return strcmp(got, "1") == 0 && runner.kept(a) == 1;
    }));
    CHECK(runner.late(a));
    CHECK(!proc::call_as(&procs, a, Note{got, '4'}, proc::kKept) && runner.kept(a) == 2);
    gate.set();
    CHECK(back());
    CHECK(strcmp(got, "1234") == 0 && runner.kept(a) == 0 && memory.in_use() == 0);

    // The queue has an end: past it, calls are lost.
    got[0] = 0;
    gate.wait(0);
    CHECK(!proc::call_as(&procs, a, Hang{&gate, got, 'h'}, proc::kKept));
    for (int i = 0; i < prog::Runner::kMostKept + 5; i++) {
        proc::call_as(&procs, a, Note{got, static_cast<char>('A' + i % 26)}, proc::kKept);
    }
    CHECK(runner.kept(a) == prog::Runner::kMostKept && memory.in_use() == prog::Runner::kMostKept);

    // Ended while it hangs: what waited for it goes with it.
    CHECK(procs.exit(a));
    CHECK(memory.in_use() == 0 && !procs.alive(a));
    gate.set();
    CHECK(soon([&] {
        runner.serve();
        return runner.tasks() == 0;
    }));
    CHECK(strlen(got) == 1); // none of them was made

    // No Memory port: nothing is kept, as before there was a queue.
    const Pid b = procs.spawn("b", root, proc::kWindowed);
    runner.set_memory(nullptr);
    CHECK(runner.start(b));
    got[0] = 0;
    gate.wait(0);
    CHECK(!proc::call_as(&procs, b, Hang{&gate, got, 'h'}, proc::kKept));
    CHECK(!proc::call_as(&procs, b, Note{got, 'b'}, proc::kKept) && runner.kept(b) == 0);
    gate.set();
    CHECK(soon([&] {
        runner.serve();
        return !runner.late(b);
    }));
    CHECK(strcmp(got, "h") == 0);
    CHECK(procs.exit(b) && runner.tasks() == 0);
}

} // namespace

int main()
{
    using proc::Pid;
    const std::thread::id ui = std::this_thread::get_id();
    proc::Table procs;
    const Pid root = procs.spawn("desktop", proc::kNone);
    const Pid a = procs.spawn("a", root, proc::kWindowed);
    const Pid b = procs.spawn("b", root, proc::kWindowed);
    prog::Runner runner(procs);

    // No tasks in the machine: nothing gets one, and a call is made here.
    {
        CHECK(!runner.threaded() && !runner.start(a) && !runner.has_task(a));
        std::thread::id on;
        Pid as = proc::kNone;
        proc::call_as(&procs, a, [&] {
            on = std::this_thread::get_id();
            as = procs.current();
        });
        CHECK(on == ui && as == a && procs.current() == proc::kNone);
    }

    host::ThreadTasks tasks;
    host::ThreadEvent events[4];
    prog::Kit kits[2] = {{&events[0], &events[1]}, {&events[2], &events[3]}};
    runner.set_tasks(&tasks, kits, 2);
    CHECK(runner.threaded());

    // A task of its own: its stack is the program's memory, and its calls
    // are made there while the caller waits.
    CHECK(runner.start(a) && runner.has_task(a) && runner.tasks() == 1 && tasks.started == 1);
    CHECK(runner.start(a) && tasks.started == 1); // it has one already
    CHECK(procs.find(a)->memory == prog::Runner::kStackBytes);
    std::thread::id a_thread;
    {
        Pid as = proc::kNone;
        int calls = 0;
        proc::call_as(&procs, a, [&] {
            a_thread = std::this_thread::get_id();
            as = procs.current();
            calls++;
        });
        CHECK(calls == 1 && a_thread != ui && as == a && procs.current() == proc::kNone);
        // The same task every time.
        std::thread::id again;
        proc::call_as(&procs, a, [&] { again = std::this_thread::get_id(); });
        CHECK(again == a_thread);
    }
    // A process without a task is still called here.
    {
        std::thread::id on;
        proc::call_as(&procs, b, [&] { on = std::this_thread::get_id(); });
        CHECK(on == ui);
    }

    // Calls inside calls. A program that calls into itself does it where it
    // is; into another with a task, it waits for that one; and when that
    // one calls back into the first, which is waiting, it is made there.
    CHECK(runner.start(b) && runner.tasks() == 2);
    CHECK(!runner.start(root)); // both kits are lent
    {
        std::thread::id self, other, back;
        Pid back_as = proc::kNone, after = proc::kNone;
        proc::call_as(&procs, a, [&] {
            proc::call_as(&procs, a, [&] { self = std::this_thread::get_id(); });
            proc::call_as(&procs, b, [&] {
                other = std::this_thread::get_id();
                proc::call_as(&procs, a, [&] {
                    back = std::this_thread::get_id();
                    back_as = procs.current();
                });
            });
            after = procs.current();
        });
        CHECK(self == a_thread && other != a_thread && other != ui && back == other);
        CHECK(back_as == a && after == a && procs.current() == proc::kNone);
    }

    // A process that ends while nothing of it runs: its task goes at once.
    CHECK(procs.exit(b));
    CHECK(runner.tasks() == 1 && tasks.ended == 1 && !runner.has_task(b));

    // One that ends in the middle of its own call (it closed its last
    // window): the call finishes, and then the task goes.
    {
        bool finished = false;
        proc::call_as(&procs, a, [&] {
            procs.exit(a);
            finished = std::this_thread::get_id() == a_thread;
        });
        CHECK(finished && runner.tasks() == 0 && tasks.ended == 2 && !procs.alive(a));
    }

    // The kit is free for the next program; no stack: no task.
    const Pid c = procs.spawn("c", root, proc::kWindowed);
    tasks.refuse = true;
    CHECK(!runner.start(c) && runner.tasks() == 0 && procs.find(c)->memory == 0);
    tasks.refuse = false;
    CHECK(runner.start(c) && runner.tasks() == 1);
    CHECK(procs.exit(c) && runner.tasks() == 0);

    test_late();
    test_queue();

    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
