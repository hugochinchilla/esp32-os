// Programs on tasks of their own (backlog item 26): the runner by itself.
// The "UI task" is the test's main thread; program tasks are real threads.
// (Culebra played on a task of its own is in test_snake.cpp.)
#include <cstdio>
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

    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
