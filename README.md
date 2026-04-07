Thread-Safe Messaging library
=============================

## Developer Note: 5-Thread Messaging Prototype

### Overview
This extension demonstrates a message-passing chain between 5 worker threads using the existing C++ messaging infrastructure.

### Extended Components
- **`src/include/dataTypes.hpp`**: Added new struct types (`FileEvent`, `AppEvent`, `ProcessedData`, `TransformedData`, `FinalResult`) to represent different stages of the processing chain.
- **`src/include/testThreads.hpp`**: Implemented 5 new worker classes inheriting from `messaging::TaskBase`:
    1. `ObserverIncomingThread`: Simulates file events and starts the chain.
    2. `ApplicationObserverThread`: Receives `FileEvent`, wraps it into `AppEvent`.
    3. `ProcessingThread`: Receives `AppEvent`, processes it into `ProcessedData`.
    4. `TransformationThread`: Receives `ProcessedData`, transforms it into `TransformedData`.
    5. `ResultThread`: Receives `TransformedData` and logs the final `FinalResult`.
- **`src/src/main.cpp`**: Instantiated, linked, and registered the 5 threads in `ThreadManager`.

### How to Run
1. Build the project:
   ```bash
   cmake --build cmake-build-release --target threadMsg
   ```
2. Run the executable:
   ```bash
   ./cmake-build-release/bin/threadMsg
   ```
3. Observe the logs showing messages flowing from `ObserverIncomingThread` to `ResultThread`.

### Verification
The demo shows that several worker threads can exchange different derived message types through a common `std::shared_ptr<MessageBase>` using the existing `MessageWrapper<T>` and `MessageQueue` mechanism. Correct shutdown is handled by `ThreadManager` and `daemon` handlers.

---

## Message Flow Diagrams

### Sequence Diagram

```mermaid
sequenceDiagram
    participant OI as ObserverIncomingThread
    participant AO as ApplicationObserverThread
    participant PT as ProcessingThread
    participant TT as TransformationThread
    participant RT as ResultThread

    Note over OI: every 2 s — generates file event

    OI->>AO: FileEvent {fileName, fileSize}
    OI->>PT: DirectEvent {counter, info}

    AO->>OI: MirrorEvent {counter, message}  ← feedback
    AO->>PT: AppEvent {appName, eventDescription, FileEvent}

    PT->>TT: ProcessedData {dataId, processingResult, AppEvent}

    TT->>RT: TransformedData {transformationType, value, ProcessedData}

    Note over RT: builds FinalResult and logs it
```

### Thread Topology

```mermaid
flowchart TD
    OI["ObserverIncomingThread\n(generates FileEvent every 2 s)"]
    AO["ApplicationObserverThread\n(wraps FileEvent → AppEvent)"]
    PT["ProcessingThread\n(processes → ProcessedData)"]
    TT["TransformationThread\n(transforms → TransformedData)"]
    RT["ResultThread\n(produces FinalResult)"]
    TM["ThreadManager\n(owns all 5 tasks,\nstop/start/restart)"]

    TM -.->|start / stopAndWait| OI
    TM -.->|start / stopAndWait| AO
    TM -.->|start / stopAndWait| PT
    TM -.->|start / stopAndWait| TT
    TM -.->|start / stopAndWait| RT

    OI -->|FileEvent| AO
    OI -->|DirectEvent| PT

    AO -->|MirrorEvent ↩ feedback| OI
    AO -->|AppEvent| PT

    PT -->|ProcessedData| TT
    TT -->|TransformedData| RT

    style OI fill:#d4e6f1,stroke:#2980b9
    style AO fill:#d5f5e3,stroke:#27ae60
    style PT fill:#fdebd0,stroke:#e67e22
    style TT fill:#f9ebea,stroke:#e74c3c
    style RT fill:#e8daef,stroke:#8e44ad
    style TM fill:#fdfefe,stroke:#7f8c8d,stroke-dasharray:5 5
```

### Message Types per Edge

| Source | Destination | Message type | Trigger |
| --- | --- | --- | --- |
| `ObserverIncomingThread` | `ApplicationObserverThread` | `FileEvent` | Every 2 s |
| `ObserverIncomingThread` | `ProcessingThread` | `DirectEvent` | Every 2 s (bypass) |
| `ApplicationObserverThread` | `ObserverIncomingThread` | `MirrorEvent` | On each `FileEvent` received |
| `ApplicationObserverThread` | `ProcessingThread` | `AppEvent` | On each `FileEvent` received |
| `ProcessingThread` | `TransformationThread` | `ProcessedData` | On each `AppEvent` received |
| `TransformationThread` | `ResultThread` | `TransformedData` | On each `ProcessedData` received |

Two notable non-linear paths:
- **Direct bypass**: `ObserverIncomingThread → ProcessingThread` via `DirectEvent` (skips `ApplicationObserverThread`)
- **Mirror feedback loop**: `ApplicationObserverThread → ObserverIncomingThread` via `MirrorEvent`

