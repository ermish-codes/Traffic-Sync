# Traffic-Sync
A multithreaded traffic and parking management simulator featuring IPC-based intersection coordination, emergency vehicle prioritization, and semaphore-controlled parking allocation.

![Status](https://img.shields.io/badge/Status-Complete-success?style=flat-square) ![Language](https://img.shields.io/badge/Language-C%2F%20POSIX-blue?style=flat-square) ![Threads](https://img.shields.io/badge/Threads-15%2B%20Vehicles-orange?style=flat-square)

**Team:** Ermish (i243180) | Hanzala (i243150) | Sawaira (i243043)  

---

## Overview

This project simulates a realistic traffic management system with:

- **2 Intersections** (F10 & F11) with traffic light control
- **15 Vehicles** with 6 different types (Ambulance, Firetruck, Bus, Car, Bike, Tractor)
- **Smart Parking System** with queue management (10 spots, 5 waiting slots per intersection)
- **Emergency Protocol** - Ambulances & Firetrucks bypass normal traffic rules
- **Inter-Process Communication (IPC)** - Pipes for emergency alerts between controllers
- **Thread Synchronization** - Mutexes, semaphores, condition variables, and atomic operations
- **Collision Avoidance** - Intelligent conflict detection for crossing vehicles

---

## Key Features

### Traffic Light System
- **Normal Mode:** Cycles between North-South (GREEN_NS) and East-West (GREEN_EW)
- **Emergency Mode:** Clears intersection for ambulances and firetrucks
- **Real-Time Control:** Dedicated light threads for each intersection

### Vehicle Management
| Type | Priority | Behavior |
|------|----------|----------|
| **AMBULANCE** | P0 | Bypasses all signals, clears path |
| **FIRETRUCK** | P0 | Bypasses all signals, clears path |
| **BUS** | P2 | Follows all rules, can park |
| **CAR** | P3 | Follows all rules, can park |
| **BIKE** | P4 | Follows all rules, can park |
| **TRACTOR** | P5 | Follows all rules, can park |

### Parking System
- **10 Parking Spots** per intersection
- **5-Vehicle Queue** system with first-come-first-serve
- **Emergency Bypass** - Vehicles wait during emergency protocols
- **Queue Management** - Automatic promotion from queue to spot

### Collision Detection
Prevents conflicts using **directional conflict matrix**:
- Same entry direction = conflict
- Same exit direction = conflict
- Straight vs. Left turns = conditional conflict
- Right turns (generally safe) = no conflict

---

## Technical Architecture

### Synchronization Primitives

```c
// Thread-Safe Global Control
pthread_mutex_t running_mutex        // Controls simulation state
pthread_mutex_t print_mutex          // Serializes console output
pthread_mutex_t stats_mutex          // Protects statistics counters

// Semaphores for Parking
sem_t lot->spots                     // Tracks available parking spots
sem_t lot->queue                     // Manages waiting queue

// Intersection Synchronization
pthread_mutex_t inter->lock          // Protects intersection state
pthread_cond_t inter->clear_cond     // Signals intersection cleared
```

### Process Model

```
Main Process (PID: parent)
├── F10 Controller Process (PID: child1) -- Manages F10 intersection
├── F11 Controller Process (PID: child2) -- Manages F11 intersection
│
└── Thread Pool:
    ├── 15 Vehicle Threads -- Execute vehicle simulation
    ├── 2 Light Threads -- Control traffic signals
    └── 1 ACK Reader Thread -- Listens for emergency confirmations
```

### Data Flow

```
Vehicle Thread
  ├─> Spawn with delay
  ├─> Check parking (optional)
  ├─> Send emergency alert (if ambulance/firetruck)
  ├─> Wait for intersection clearance
  ├─> Cross first intersection
  ├─> Travel between intersections
  └─> Cross second intersection

IPC Flow:
  Vehicle sends: "EMERGENCY:AMBULANCE#5 from F10"
       ↓ (via pipe)
  F10 Controller receives and processes
       ↓ (trigger emergency mode)
  F10 clears existing vehicles
       ↓ (sends ACK)
  ACK Reader receives: "ACK_EMERGENCY:5"
       ↓ (sets flag)
  Vehicle proceeds through intersection
```

---

## Input File Format

**vehicles.txt** - 15 lines, each with 7 space-separated integers:

```
<type> <origin> <direction> <movement> <destination> <wants_park> <spawn_delay_ms>
```

### Field Definitions

| Field | Values | Meaning |
|-------|--------|---------|
| **type** | 0-5 | 0=AMBULANCE, 1=FIRETRUCK, 2=BUS, 3=CAR, 4=BIKE, 5=TRACTOR |
| **origin** | 0-1 | 0=F10, 1=F11 |
| **direction** | 0-3 | 0=NORTH, 1=SOUTH, 2=EAST, 3=WEST |
| **movement** | 0-2 | 0=STRAIGHT, 1=LEFT, 2=RIGHT |
| **destination** | 0-1 | 0=F10, 1=F11 |
| **wants_park** | 0-1 | 0=NO, 1=YES (ignored for ambulances/firetrucks) |
| **spawn_delay_ms** | 0-5000 | Milliseconds to wait before spawning |

### Example

```
# Vehicle 1: Car at F10, North, Straight, to F11, wants parking, 500ms delay
3 0 0 0 1 0 500

# Vehicle 2: Ambulance at F10, North, Left turn, to F11, no park, 0ms delay
0 0 0 1 1 0 0

# Vehicle 3: Tractor at F11, West, Right turn, to F10, wants park, 200ms delay
5 1 3 2 0 1 200
```

---

## Building & Running

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install build-essential libpthread-stubs0-dev

# macOS (Homebrew)
brew install gcc
```

### Compilation

```bash
# Compile the program
gcc -o traffic_sim i243180_i243043_i243150_SeC.c -pthread -lm -Wall -std=c99

# Or with optimization
gcc -O2 -o traffic_sim i243180_i243043_i243150_SeC.c -pthread -lm -Wall -std=c99

# Or with debug symbols
gcc -g -o traffic_sim i243180_i243043_i243150_SeC.c -pthread -lm -Wall -std=c99
```

### Running

```bash
# Run the simulation
./traffic_sim

# When prompted, enter the input file name:
# vehicles.txt
```

### Expected Output

```
===========================================================
|                   TRAFFIC SIMULATION                    |
|                 OS Project Spring 2026                  |
===========================================================

  Enter input file name (e.g., vehicles.txt): vehicles.txt

  Reading vehicles from 'vehicles.txt'...

  Vehicle #1: CAR       | F10/NORTH | STRAIGHT | → F11 | Park:NO | Delay:500ms
  Vehicle #2: AMBULANCE | F10/NORTH | LEFT     | → F11 | Park:NO | Delay:0ms
  ...
  
  =============================================================================================
  |                        VEHICLE CONFIGURATION SUMMARY                                      |
  =============================================================================================
  | # | Type      | Prio | Origin  | From Dir  | Dest       | Move       | Park?   | Delay    |
  ...
  
  Press ENTER to start the simulation...

============================================================
|                   SIMULATION STARTING                    |
============================================================

[SETUP] IPC pipes created
[SETUP] SIGINT handler registered
[SETUP] Controller processes: F10(PID:12345) F11(PID:12346)

[PARKING] Lot at F10 initialized (10 spots, queue 5)
[PARKING] Lot at F11 initialized (10 spots, queue 5)
[INIT] Intersection F10 ready
[INIT] Intersection F11 ready

[LIGHT] F10 → GREEN North-South
[LIGHT] F11 → GREEN North-South

[SPAWN] Vehicle #1 | CAR       | Priority:3 | Origin:F10/NORTH | Move:STRAIGHT | Park:NO
[CROSSING] Vehicle #1 (CAR) [P3] entering F10 from NORTH going STRAIGHT
[CROSSED]  Vehicle #1 (CAR) cleared F10

[EMERGENCY] Vehicle #2 (AMBULANCE) triggered EMERGENCY MODE at F10
...

=========================================
|           FINAL STATISTICS            |
|  Vehicles crossed intersections: 15   |
|  Vehicles parked:                5    |
|  Emergency alerts sent:          2    |
=========================================

|       SIMULATION COMPLETED SUCCESSFULLY                  |
```

---

## Testing Scenarios

### Test 1: Race Conditions
Create vehicles with **same intersection, same direction, 0ms delay**:

```
3 0 0 0 1 0 0
3 0 0 0 1 0 0
3 0 0 0 1 0 0
```

**Expected:** Conflict detection prevents simultaneous crossings.

### Test 2: Emergency Priority
Use **AMBULANCE/FIRETRUCK** vehicles:

```
0 0 0 0 1 0 0      # Ambulance - clears immediately
3 0 0 0 1 0 0      # Regular car - waits
```

**Expected:** Ambulance crosses first, bypassing traffic signals.

### Test 3: Parking Overflow
Set **>10 vehicles wanting to park** at same intersection:

```
3 0 0 0 0 1 100
3 0 1 0 0 1 100
3 0 2 0 0 1 100
3 0 3 0 0 1 100
3 1 0 0 0 1 100
3 1 1 0 0 1 100
3 1 2 0 0 1 100
3 1 3 0 0 1 100
2 0 0 0 0 1 100
2 0 1 0 0 1 100
2 0 2 0 0 1 100
```

**Expected:** First 10 park, rest enter queue, 5 rejected.

### Test 4: Conflict Detection
Conflicting movements at same intersection:

```
3 0 0 0 1 0 0      # Straight from North
3 0 2 1 1 0 100    # Left from East - CONFLICT!
```

**Expected:** One waits for other to clear.

### Test 5: Cross-Intersection Travel
Vehicles traveling between both intersections:

```
3 0 0 0 1 0 0      # F10 → F11
2 1 2 0 0 0 500    # F11 → F10
5 0 3 2 1 0 1000   # F10 → F11 (tractor)
```

**Expected:** Both intersections remain synchronized.

---

## Statistics & Metrics

Final output includes:
- **Vehicles Crossed** - Total intersection crossings
- **Vehicles Parked** - Successful parking operations
- **Emergency Alerts** - Emergency protocol activations

---

## Troubleshooting

### Error: "Cannot open 'vehicles.txt'"
- Ensure `vehicles.txt` is in same directory as executable
- Check file permissions: `chmod 644 vehicles.txt`

### Error: "Segmentation fault"
- Validate `vehicles.txt` has exactly 15 valid lines
- Check input format (7 space-separated integers per line)

### Error: "Too many open files"
- Increase file descriptor limit: `ulimit -n 2048`
- Reduce `TOTAL_VEHICLES` constant

### Simulation Hangs
- Press `Ctrl+C` to gracefully shutdown
- Check for mutex deadlocks in code
- Review parking system logic

---

## Code Structure

```c
// Constants & Enums
TOTAL_VEHICLES, PARKING_SPOTS, WAITING_QUEUE_SIZE
VehicleType, Direction, Movement, IntersectionID, ParkStatus, LightState

// Structs
Vehicle              - Vehicle configuration & state
ParkingLot          - Parking lot with semaphores
Intersection        - Intersection with lights & vehicles crossing

// Core Functions
parking_init()                - Initialize parking lot
parking_enter()               - Vehicle attempts to park
parking_exit()                - Vehicle leaves parking
intersection_init()           - Initialize intersection
can_cross()                   - Check if vehicle can cross
cross_intersection()          - Handle vehicle crossing
light_cycle_thread()          - Traffic light state machine
vehicle_thread()              - Main vehicle simulation
send_emergency_alert()        - IPC emergency message
run_controller()              - Controller process main loop
ack_reader_thread()           - Listen for emergency ACKs
```

---

## Learning Outcomes

This project demonstrates:

- **Concurrent Programming** - 15+ simultaneous threads
- **Process Synchronization** - Mutexes, semaphores, condition variables
- **Inter-Process Communication** - Pipes for emergency protocol
- **Deadlock Prevention** - Careful lock ordering
- **Real-Time Systems** - Time-sensitive event handling
- **State Machine Design** - Traffic light & vehicle logic
- **Conflict Resolution** - Intelligent collision avoidance
- **Resource Management** - Parking allocation & queueing
