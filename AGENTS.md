# AGENTS.md: AI Coding Agent Guide

## Project Overview

**boilerplate-thread-messaging**: High-performance C++20 thread-safe inter-thread message passing system with a 5-thread demo chain prototype. Focus: type-safe, lock-free patterns where possible, cooperative thread cancellation via stop tokens.

**Key Constraint**: C++20 required. Uses `std::jthread`, `std::stop_token`, and type-erasure patterns (`MessageBase`/`MessageWrapper<T>`).

---

## Architecture: Two-Layer Design

### Layer 1: Reusable Messaging Primitives (`src/include/messaging/`, `src/include/threading/`)

| Component | File | Pattern |
|-----------|------|---------|
| `MessageBase` / `MessageWrapper<T>` | `messaging/messageBase.hpp` | Type-erased virtual base; `dynamic_pointer_cast<MessageWrapper<T>>` to extract payload |
| `MessageQueue` | `messaging/messageQueue.hpp` | Mutex-protected; `.wait()`, `.wait_for()`, `.try_pop()` |
| `Sender` | `messaging/messageSender.hpp` | Non-owning handle; `.Send<T>(args...)` constructs `MessageWrapper<T>` and enqueues |
| `Receiver` | `messaging/messageReceiver.hpp` | Owns `MessageQueue`; override `onPostMessageReceived()` callback |
| `TaskBase` | `taskBase.hpp` | Combines `Receiver + Sender + workerBase` (jthread owner) |
| `Task` | `taskBase.hpp` | Concrete `TaskBase` accepting lambda for `run()` |
| `workerBase` | `workerBase.hpp` | Manages one `std::jthread`; cooperative stop via `std::stop_token` |
| `SpScRingBuffer<T>` | `threading/spScRingBuffer.hpp` | Lock-free SPSC (single-producer, single-consumer); power-of-two capacity; `.wait_pop_for()` |
| `ThreadSafeQueue<T>` | `threading/threadSafeQueue.hpp` | Generic mutex-protected queue (rarely used in demo) |

### Layer 2: 5-Thread Demo Chain (`src/include/testThreads.hpp`, `dataTypes.hpp`)

All five threads inherit from `TaskBase`. Message data types in `dataTypes.hpp`:
- `FileEvent` (name, size, timestamp)
- `AppEvent` (appName, eventDescription, originalFile)
- `ProcessedData` (dataId, result, AppEvent)
- `TransformedData` (type, value, ProcessedData)
- `FinalResult` (success, summary, TransformedData)
- `MirrorEvent` (counter, message)
- `DirectEvent` (counter)
- `ObserverCommand` (type, durationSec)

**Message flows:**
```
ObserverIncomingThread → ApplicationObserverThread → ProcessingThread → TransformationThread → ResultThread
     ↓                              ↓                                    ↓
     DirectEvent bypass ────────────›                     MirrorEvent loop back
```

---

## Critical Patterns & Developer Workflows

### 1. Creating a New Thread Worker

Inherit from `TaskBase` and implement three methods:

```cpp
class MyWorker : public messaging::TaskBase {
  void run(std::stop_token stopToken) override {
    while (!stopToken.stop_requested()) {
      auto msg = messageQueue().wait_for(std::chrono::milliseconds(100));
      if (msg) {
        if (auto typed = std::dynamic_pointer_cast<MessageWrapper<MyMessageType>>(msg)) {
          // Process
        }
      }
    }
  }

  void onPostMessageReceived(const std::type_info&, const std::shared_ptr<MessageBase>&) override {
    // Called immediately after push(); used to wake sleeping threads
  }

  bool isReadyToStart() noexcept override { return true; }
};
```

**Key**: Always override all three methods. `onPostMessageReceived()` is used to signal threads waiting on timeout-based checks.

### 2. Wiring Two Tasks (e.g., in ThreadManager)

```cpp
auto sender = downstreamTask->makeSender(notifyOnPost);
upstreamTask->setSender(sender);
upstreamTask->Send(message);
```

**notifyOnPost=false**: MessageQueue::push() already calls `notify_one()`; don't double-wake.
**notifyOnPost=true**: Use when consumer polls without timeout (rare).

### 3. Out-of-Band Control: ObserverCommand via SpScRingBuffer

The `ObserverIncomingThread` receives commands via a **separate** lock-free SPSC ring buffer (not the MessageQueue):

```cpp
ObserverCommand cmd{ObserverCommand::Type::StressMode};
m_cmdBuffer.push(cmd);
m_cmdBuffer.notifyOne();
```

Consumer side in `run()`:
```cpp
ObserverCommand cmd;
if (m_cmdBuffer.wait_pop_for(cmd, m_interval)) {
  applyCommand(cmd);
}
```

**Why**: Commands must be processed immediately without interfering with the normal message queue timing. Ring buffer is lock-free for high-frequency control.

### 4. Console Command Handler (main.cpp)

Single-character stdin commands trigger ThreadManager lifecycle calls:
- `p` → `sendObserverCommand(StopObserving)` (SPSC buffer)
- `x` → `sendStressModeCommand()` (stress mode activates 10ms interval, auto-reverts after 30s)
- `s`, `r`, `t` → `stopAllThreads()`, `restartAllThreads()`, `terminateAllThreads()`

**Pattern**: Parse char from stdin, dispatch enum, call ThreadManager method. See `main.cpp` lines ~150–250.

### 5. Thread-Safe Logging

Use `utils::safe_print()` macro to avoid interleaved output:

```cpp
#include "memoryUtils.hpp"
messaging::safe_print("[MyThread] status: ", value);
```

Internally acquires `utils::getLogMutex()`. All test threads use this.

---

## Build & Test Workflow

```bash
# Debug config (default CMAKE_BUILD_TYPE)
cmake -B build -S .

# Release config
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build parallel
cmake --build build -- -j$(nproc)

# Run foreground (accepts stdin commands)
./build/bin/threadMsg

# Run daemon (background)
./build/bin/threadMsg -D

# Run tests
cd build && ctest
```

**Options** (CMakeLists.txt):
- `BUILD_SHARED_LIBS` (default OFF)
- `BUILD_TESTING` (default ON)
- `BUILD_DOC` (default ON, generates Doxygen)

**Compiler**: C++20 required; no extensions; `-Wall`, `-O3` (Release), `-g` (Debug).

---

## Key Implementation Details

### TaskBase Lifecycle

1. **Construction**: Initializes `Receiver`, `Sender`, `workerBase`.
2. **start()**: Spawns `std::jthread` running `run(stopToken)`.
3. **sendMessage()**: Calls `onPostMessageReceived()` callback immediately after push.
4. **stopAndWait()**: Requests stop token, joins thread.
5. **Non-copyable/non-movable**: Enforced by `workerBase` (jthread member).

### Message Type-Erasure Flow

```
Sender::Send<T>(value)
  ↓
Create MessageWrapper<T>{value}
  ↓
Enqueue std::shared_ptr<MessageBase>
  ↓
Receiver pops and dynamic_cast to MessageWrapper<T>
```

**No runtime overhead** if cast fails (just a nullptr check).

### ObserverIncomingThread Modes

| Mode | Interval | Entry | Exit |
|------|----------|-------|------|
| Normal | 2000ms | init / stress auto-revert | — |
| Stress | 10ms | `StressMode` command | 30s auto-revert or `SetNormalMode` |
| SetNormal | 500ms | `SetNormalMode` command | next mode command |

**Auto-revert**: After 30s in Stress, reverts to Normal (2000ms). Prevents runaway event generation.

### MirrorEvent Feedback Loop

`ApplicationObserverThread` mirrors each received `FileEvent` back to `ObserverIncomingThread` via `MirrorEvent`. Useful for validating that events propagated through the chain.

### DirectEvent Bypass

`ObserverIncomingThread` sends `DirectEvent` directly to `ProcessingThread`, skipping `ApplicationObserverThread`. Demonstrates branching/merging in message topology.

---

## Common Pitfalls & Fixes

| Issue | Cause | Fix |
|-------|-------|-----|
| Threads hang on message wait | No sender connected | Verify `makeSender()` called and passed downstream |
| Double-wake overhead | `notifyOnPost=true` on fast path | Use `notifyOnPost=false` |
| Output garbled | Concurrent prints to stdout | Use `utils::safe_print()` |
| Stop token ignored | Forgot `while (!stopToken.stop_requested())` | Always check stop token in loop condition |
| Memory leak | TaskBase kept alive after thread exit | Use `shared_ptr<TaskBase>` in ThreadManager; clear on `terminateAllThreads()` |
| Type mismatch on cast | Sent different message type | Check type info in `dynamic_pointer_cast<>` |

---

## File Navigation Cheat Sheet

```
src/include/
├── messaging/
│   ├── messageBase.hpp      ← Type-erasing wrapper, MessageWrapper<T>
│   ├── messageQueue.hpp     ← Mutex queue, wait/wait_for/try_pop
│   ├── messageSender.hpp    ← Send<T>() interface, non-owning
│   └── messageReceiver.hpp  ← Receiver, onPostMessageReceived hook
├── threading/
│   ├── spScRingBuffer.hpp   ← Lock-free SPSC for commands
│   └── threadSafeQueue.hpp  ← Generic mutex queue (rarely used)
├── taskBase.hpp             ← TaskBase + concrete Task class
├── workerBase.hpp           ← jthread owner, cooperative stop
├── testThreads.hpp          ← Five thread implementations
├── threadManager.hpp        ← Owns, wires, and controls all five
├── dataTypes.hpp            ← Message struct definitions
├── daemon.hpp, daemonConfig.hpp ← Daemon harness
└── memoryUtils.hpp          ← safe_print, memory tracking

src/src/
└── main.cpp                 ← Console loop, stdin command dispatch
```

---

## Adding a New Message Type

1. **Define struct** in `dataTypes.hpp`:
   ```cpp
   struct MyEvent { std::string data; };
   ```

2. **Send from producer thread**:
   ```cpp
   sender.Send(MyEvent{"payload"});
   ```

3. **Receive in consumer thread** (in `run()`):
   ```cpp
   if (auto msg = std::dynamic_pointer_cast<MessageWrapper<MyEvent>>(popMsg)) {
     std::string data = msg->contents_.data;
   }
   ```

No registration needed; type-erasing works automatically.

---

## Testing Conventions

- **Unit tests**: `test/task_test.cpp` demonstrates:
  - Creating a custom `TaskBase` subclass
  - Binding a task to itself as sender
  - Sending/receiving a message
  - Using concrete `Task` with lambda
  - Assertions on receive/notification counts
- **Integration**: Run `./build/bin/threadMsg` in foreground, use console keys to drive events.
- **Stress testing**: `x` key triggers 10ms interval for 30s; monitor memory via logs.

---

## Performance Considerations

- **Lock-free SPSC for commands**: Avoids mutex contention on high-frequency control signals.
- **MessageQueue mutex**: Acceptable for inter-task messaging (lower frequency than command buffer).
- **Cooperative cancellation**: `std::stop_token` avoids forced kills; threads clean up gracefully.
- **Type-erasure zero-cost**: Virtual dispatch only during enqueue/dequeue, not iteration.
- **safe_print() contention**: Acceptable for logging; not on hot path. Use sparingly if latency-critical.

---

## Extending the Demo Chain

To add a 6th thread processing `MyType`:

1. Create new `class SixthThread : public TaskBase` in `testThreads.hpp`.
2. Implement `run()`, `onPostMessageReceived()`, `isReadyToStart()`.
3. Store `m_next` sender to downstream thread (or final sink).
4. In `ThreadManager::initTestChain()`:
   - Create instance
   - Wire upstream: `upstreamThread->setNext(sixthThread->makeSender(false))`
   - Add: `addTask(sixthThread)`

No other changes needed; everything is template-based and compose-at-runtime.

