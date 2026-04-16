/**
 * @file example_game_loop.cpp
 * @brief Demonstrates how to integrate QuickJSBinder in a game engine
 *        with a per-frame event loop — no blocking on the render thread.
 *
 * Key idea: evalModule() returns a Promise for modules with top-level await.
 * Instead of calling ctx.await() (which blocks), we drive microtasks and
 * I/O each frame with pollIO(0) + loopOnce().
 */

#include "common.hpp"
#include <iostream>
#include <chrono>
#include <thread>

using namespace qjsbind;

// Simulate a 60fps game tick (~16ms per frame)
static constexpr int kFrameMs = 16;

int main() {
    // ===================================================================
    // 1. Setup: same as any other use case
    // ===================================================================
    JsRuntime rt;
    rt.enableModuleLoader();

    JsContext ctx(rt);
    ctx.addBuiltinModules();

    // ===================================================================
    // 2. Load a module that uses top-level await
    // ===================================================================
    std::cout << "=== Loading async module (non-blocking) ===" << std::endl;

    auto result = ctx.evalModule(R"(
        import * as os from 'os';

        // Simulate async initialization (e.g. loading assets, fetching config)
        const config = await new Promise(resolve => {
            os.setTimeout(() => {
                resolve({ difficulty: 'hard', level: 3 });
            }, 100);  // 100ms async init
        });

        console.log('Game initialized:', JSON.stringify(config));
        globalThis.gameConfig = config;
        globalThis.gameReady  = true;
    )", nullptr, "<game_init>");

    if (result.isException()) {
        ctx.dumpError();
        return 1;
    }

    // At this point, result is a Promise (because of top-level await).
    // We do NOT call ctx.await() — that would block the render thread!

    // ===================================================================
    // 3. Game-engine style main loop
    // ===================================================================
    std::cout << "=== Entering game loop ===" << std::endl;

    bool gameReady = false;
    int frame = 0;

    while (frame < 200) {  // safety limit
        auto frameStart = std::chrono::steady_clock::now();

        // --- JS tick: drive microtasks + I/O (non-blocking) ---
        ctx.pollIO(0);     // timeout=0 → non-blocking poll
        ctx.loopOnce();    // process one pending microtask/job

        std::cout << "Main loop running... frame: " << frame << std::endl;

        // --- Game logic ---
        if (!gameReady) {
            JsValue ready = ctx.eval("globalThis.gameReady || false");
            if (ready.toBool()) {
                gameReady = true;
                JsValue cfg = ctx.eval(
                    "JSON.stringify(globalThis.gameConfig)");
                std::cout << "  [frame " << frame << "] Game ready! "
                          << "config = " << cfg.toString() << std::endl;
            }
        }

        if (gameReady) {
            // Normal game frame — render, physics, etc.
            // For demo purposes, run a few more frames then exit
            if (frame % 10 == 0) {
                std::cout << "  [frame " << frame << "] Rendering..."
                          << std::endl;
            }

            // Let's say we quit after 20 frames post-ready
            static int readyFrames = 0;
            if (++readyFrames >= 20) break;
        }

        frame++;

        // --- Frame pacing ---
        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        auto sleepTime = std::chrono::milliseconds(kFrameMs) - elapsed;
        if (sleepTime.count() > 0) {
            std::this_thread::sleep_for(sleepTime);
        }
    }

    std::cout << "=== Game loop finished after " << frame << " frames ==="
              << std::endl;
    return 0;
}
