# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test

Requires C++20 and GCC/Clang with C++20 support (e.g. `g++-13`).

```bash
# Configure
CXX=/usr/bin/g++-13 cmake -B build -S .

# Build (debug default)
cmake --build build -j$(nproc)

# Release
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)

# Run all tests
cd build && ctest

# Run single test verbosely
cd build && ctest -V -R task_test
```

Binaries land in `build/bin/`. `compile_commands.json` is generated automatically.

## Architecture

Two distinct layers:

### Layer 1 — Reusable threading + messaging primitives (`src/include/`)

- **`workerBase`** — owns one `std::jthread`. Subclass overrides `run(std::stop_token)`, `isReadyToStart()`. Provides `start(externalToken={})`, `stop()`, `stopAndWait()`, `join()`, `wakeUp()`, and `waitFor(token, timeout, pred)`.
- **`TaskBase`** — inherits `Receiver + Sender + workerBase`. The base for every worker thread. Three pure-virtual methods: `run`, `onPostMessageReceived`, `isReadyToStart`.
- **`Task`** — concrete `TaskBase` taking a `RunFunction` lambda; used for ad-hoc threads.
- **`Sender` / `Receiver`** — type-erased message passing via `shared_ptr<MessageBase>`. `Sender::Send<T>(msg)` wraps in `MessageWrapper<T>` and enqueues. Receiver calls `onPostMessageReceived()` after a `dynamic_pointer_cast` to the concrete type.
- **`SpScRingBuffer`** — lock-free SPSC ring buffer (power-of-two capacity). Used for out-of-band control commands, separate from the main `MessageQueue`.

### Layer 2 — Demo 5-thread pipeline (`src/include/testThreads.hpp`, `src/include/dataTypes.hpp`)

```
ObserverIncoming → ApplicationObserver → Processing → Transformation → Result
       ↑                   ↓ (mirror)
       └───────────────────┘
```

Each thread is a `TaskBase` subclass. `ThreadManager` creates, wires (via `makeSender()`), and owns all of them.

### Stop-token flow

`ThreadManager` owns a single `std::stop_source m_stopSource`. On `addTask()`, the token is forwarded to `workerBase::start(externalToken)`. Inside the jthread lambda, the external token and the jthread's own internal token are **merged** via two `std::stop_callback`s into a single combined `std::stop_source` — so both `m_stopSource.request_stop()` (global stop) and `task->stop()` (individual stop) work correctly.

`stopAllThreads()` calls `m_stopSource.request_stop()` then `wakeUp()` + `join()` on each task. `restartAllThreads()` resets `m_stopSource` to a fresh instance before restarting.

## Key Patterns

**Adding a new thread worker** — subclass `TaskBase`, override the three pure-virtual methods, wire it via `threadManager.addTask(instance)`. The `start()` call and stop-token forwarding are handled by `addTask`.

**Wiring message flow** — `taskA->setNext(taskB->makeSender(notifyOnPost))`. `notifyOnPost=false` when the receiver's `MessageQueue::push()` already calls `notify_one()` (avoids double-wake).

**Out-of-band commands** — use `SpScRingBuffer` (as `ObserverIncomingThread` does) for lock-free control signals that bypass the main message queue.

**Thread-safe logging** — use `utils::safe_print()` (mutex-guarded), not `std::cout` directly.
