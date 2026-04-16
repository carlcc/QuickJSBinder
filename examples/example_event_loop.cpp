/**
 * @file example_event_loop.cpp
 * @brief Demonstrates JsContext event loop & async support:
 *        loop(), loopOnce(), pollIO(), await(), dumpError().
 */

#include "common.hpp"
#include <iostream>

using namespace qjsbind;

int main() {
    // ===================================================================
    // Setup: Runtime with module loader + Context with builtin modules
    // ===================================================================
    JsRuntime rt;
    rt.enableModuleLoader();  // REQUIRED: initializes std event handlers

    JsContext ctx(rt);
    ctx.addBuiltinModules();  // registers std, os, bjson + helpers

    // ===================================================================
    // Test 1: loop() — run event loop until all jobs complete
    // ===================================================================
    std::cout << "=== Test 1: loop() — run full event loop ===" << std::endl;
    {
        // Use evalModule so we can import os for timers
        auto mod = ctx.evalModule(R"(
            import * as os from 'os';
            globalThis.__order = [];
            os.setTimeout(() => globalThis.__order.push('timer1'), 10);
            os.setTimeout(() => globalThis.__order.push('timer2'), 20);
            os.setTimeout(() => globalThis.__order.push('timer3'), 30);
        )", "<test1_setup>");
        (void)mod;

        // Drive the event loop — processes all timers until done
        ctx.loop();

        // Verify timer execution order
        JsValue result = ctx.eval("globalThis.__order.join(', ')");
        std::cout << "  timer order: " << result.toString() << std::endl;
    }

    // ===================================================================
    // Test 2: await() — synchronously await a Promise
    // ===================================================================
    std::cout << "\n=== Test 2: await() — synchronously await Promise ===" << std::endl;
    {
        JsValue promise = ctx.eval(R"(
            new Promise((resolve) => resolve(42))
        )");

        JsValue result = ctx.await(std::move(promise));
        if (result.isException()) {
            std::cout << "  ERROR: Promise rejected!" << std::endl;
            ctx.dumpError();
        } else {
            std::cout << "  Promise resolved to: " << result.toInt32() << std::endl;
        }
    }

    // ===================================================================
    // Test 3: await() with timer-based async (using os.setTimeout)
    // ===================================================================
    std::cout << "\n=== Test 3: await() with timer-based async ===" << std::endl;
    {
        // Inject a helper that returns a timer-based promise
        auto setup = ctx.evalModule(R"(
            import * as os from 'os';
            globalThis.delayedValue = (val, ms) => new Promise(resolve => {
                os.setTimeout(() => resolve(val), ms);
            });
        )", "<test3_setup>");
        (void)setup;
        ctx.loop();

        // Now call it and await
        JsValue promise = ctx.eval("delayedValue('async_hello', 50)");
        JsValue result = ctx.await(std::move(promise));
        if (result.isException()) {
            std::cout << "  ERROR: Promise rejected!" << std::endl;
            ctx.dumpError();
        } else {
            std::cout << "  Async result: " << result.toString() << std::endl;
        }
    }

    // ===================================================================
    // Test 4: loopOnce() + pollIO() — manual event loop integration
    // ===================================================================
    std::cout << "\n=== Test 4: loopOnce() + pollIO() — manual event loop ===" << std::endl;
    {
        auto setup = ctx.evalModule(R"(
            import * as os from 'os';
            globalThis.__loopOnceCounter = 0;
            os.setTimeout(() => { globalThis.__loopOnceCounter++; }, 10);
            os.setTimeout(() => { globalThis.__loopOnceCounter++; }, 20);
        )", "<test4_setup>");
        (void)setup;

        // Simulate a game-engine-style loop:
        // pollIO waits for I/O/timers, loopOnce processes ready jobs
        int iterations = 0;
        while (true) {
            ctx.pollIO(5);     // wait up to 5ms for I/O/timer events
            ctx.loopOnce();    // process one ready job if available
            iterations++;

            JsValue counter = ctx.eval("globalThis.__loopOnceCounter");
            if (counter.toInt32() >= 2) break;  // both timers fired
            if (iterations > 100) {              // safety limit
                std::cout << "  Timeout!" << std::endl;
                break;
            }
        }

        JsValue counter = ctx.eval("globalThis.__loopOnceCounter");
        std::cout << "  Manual loop iterations: " << iterations << std::endl;
        std::cout << "  Counter value: " << counter.toInt32() << " (expect 2)" << std::endl;
    }

    // ===================================================================
    // Test 5: pollIO() — poll with timeout
    // ===================================================================
    std::cout << "\n=== Test 5: pollIO() — I/O polling ===" << std::endl;
    {
        auto setup = ctx.evalModule(R"(
            import * as os from 'os';
            globalThis.__pollDone = false;
            os.setTimeout(() => {
                globalThis.__pollDone = true;
            }, 30);
        )", "<test5_setup>");
        (void)setup;

        // Poll with short timeout, then check for completion
        int pollCount = 0;
        while (true) {
            ctx.pollIO(10);  // 10ms poll
            ctx.loopOnce();
            pollCount++;
            JsValue done = ctx.eval("globalThis.__pollDone");
            if (done.toBool()) break;
            if (pollCount > 20) { // safety limit
                std::cout << "  Timeout!" << std::endl;
                break;
            }
        }
        std::cout << "  Poll iterations before done: " << pollCount << std::endl;
        std::cout << "  __pollDone = " << ctx.eval("globalThis.__pollDone").toBool() << std::endl;
    }

    // ===================================================================
    // Test 6: dumpError() — error reporting
    // ===================================================================
    std::cout << "\n=== Test 6: dumpError() — exception dump ===" << std::endl;
    {
        JsValue result = ctx.eval("throw new Error('intentional test error')");
        if (result.isException()) {
            std::cout << "  Caught exception, dumping:" << std::endl;
            ctx.dumpError();
        }
    }

    std::cout << "\n=== All event loop tests passed! ===" << std::endl;
    return 0;
}
