/******************************************
 Name:      Hanzala      Ermish        Sawaira
 Rollno:    i243150      i243180       i243043
 Section:   Se-C
 Porject:   Traffic Simulation
 ******************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>

/*============================================================
CONSTANTS
============================================================ */
#define TOTAL_VEHICLES     15
#define PARKING_SPOTS      10
#define WAITING_QUEUE_SIZE  5
#define NUM_INTERSECTIONS   2
#define MAX_VEHICLE_NAME   20

typedef enum { AMBULANCE = 0, FIRETRUCK = 1, BUS = 2, CAR = 3, BIKE = 4, TRACTOR = 5 } VehicleType;
typedef enum { NORTH = 0, SOUTH = 1, EAST = 2, WEST = 3 } Direction;
typedef enum { STRAIGHT = 0, LEFT = 1, RIGHT = 2 } Movement;
typedef enum { F10 = 0, F11 = 1 } IntersectionID;
typedef enum { NO_PARK = 0, PARKED = 1, IN_QUEUE = 2, PARK_FAILED = 3 } ParkStatus;
typedef enum { GREEN_NS = 0, GREEN_EW = 1, EMERGENCY_MODE = 2 } LightState;


//VEHICLE STRUCT
typedef struct {
    int             id;
    VehicleType     type;
    char            type_name[MAX_VEHICLE_NAME];
    IntersectionID  origin_intersection;
    Direction       origin_direction;
    IntersectionID  destination_intersection;
    Movement        movement;
    int             priority;
    int             wants_to_park;
    ParkStatus      park_status;
    unsigned int    spawn_delay_ms;   //ms to wait before spawning this vehicle 
} Vehicle;


//PARKING LOT
typedef struct {
    sem_t           spots;
    sem_t           queue;
    pthread_mutex_t lock;
    int             available_spots;
    int             waiting_count;
    IntersectionID  location;
} ParkingLot;

//INTERSECTION
typedef struct {
    IntersectionID  id;
    LightState      light_state;
    volatile int    emergency_active;
    pthread_mutex_t lock;
    pthread_cond_t  clear_cond;
    int             vehicles_crossing[4][3];
} Intersection;

//GLOBALS
volatile sig_atomic_t running = 1;
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

Intersection f10_intersection, f11_intersection;
ParkingLot   f10_parking, f11_parking;

int pipe_to_f10[2];
int pipe_to_f11[2];
int pipe_from_f10[2];
int pipe_from_f11[2];

pid_t f10_pid, f11_pid;

volatile int f10_cleared = 0;
volatile int f11_cleared = 0;
pthread_mutex_t cleared_mutex = PTHREAD_MUTEX_INITIALIZER;

int vehicles_crossed = 0;
int vehicles_parked = 0;
int emergency_alerts = 0;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

//HELPER FUNCTIONS
const char* type_name(VehicleType t) {
    switch (t) {
    case AMBULANCE: return "AMBULANCE"; case FIRETRUCK: return "FIRETRUCK";
    case BUS:       return "BUS";       case CAR:       return "CAR";
    case BIKE:      return "BIKE";      case TRACTOR:   return "TRACTOR";
    }
    return "UNKNOWN";
}
const char* dir_str(Direction d) {
    switch (d) {
    case NORTH: return "NORTH"; case SOUTH: return "SOUTH";
    case EAST:  return "EAST";  case WEST:  return "WEST";
    }
    return "?";
}
const char* mov_str(Movement m) {
    switch (m) {
    case STRAIGHT: return "STRAIGHT"; case LEFT: return "LEFT";
    case RIGHT:    return "RIGHT";
    }
    return "?";
}
const char* inter_str(IntersectionID i) { return i == F10 ? "F10" : "F11"; }
int is_emergency(const Vehicle* v) { return v->type == AMBULANCE || v->type == FIRETRUCK; }


//DIRECTION CALCULATION FOR CROSS-INTERSECTION TRAVEL
Direction get_exit_direction(Direction entry, Movement mov) {
    if (mov == STRAIGHT) {
        switch (entry) {
        case NORTH: return SOUTH;
        case SOUTH: return NORTH;
        case EAST:  return WEST;
        default:    return EAST;
        }
    }
    else if (mov == RIGHT) {
        switch (entry) {
        case NORTH: return WEST;
        case SOUTH: return EAST;
        case EAST:  return NORTH;
        default:    return SOUTH;
        }
    }
    else {
        switch (entry) {
        case NORTH: return EAST;
        case SOUTH: return WEST;
        case EAST:  return SOUTH;
        default:    return NORTH;
        }
    }
}

Direction get_arrival_direction(Direction exit_dir) {
    switch (exit_dir) {
    case NORTH: return SOUTH;
    case SOUTH: return NORTH;
    case EAST:  return WEST;
    default:    return EAST;
    }
}

//SAFE PRINT
#define PRINT(...) do { \
    pthread_mutex_lock(&print_mutex); \
    printf(__VA_ARGS__); \
    fflush(stdout); \
    pthread_mutex_unlock(&print_mutex); \
} while(0)

//PARKING LOT  
void parking_init(ParkingLot* lot, IntersectionID loc) {
    sem_init(&lot->spots, 0, PARKING_SPOTS);
    sem_init(&lot->queue, 0, WAITING_QUEUE_SIZE);
    pthread_mutex_init(&lot->lock, NULL);
    lot->available_spots = PARKING_SPOTS;
    lot->waiting_count = 0;
    lot->location = loc;
    PRINT("[PARKING] Lot at %s initialized (%d spots, queue %d)\n",
        inter_str(loc), PARKING_SPOTS, WAITING_QUEUE_SIZE);
}

ParkStatus parking_enter(Vehicle* v, ParkingLot* lot) {
    Intersection* inter = (lot->location == F10) ? &f10_intersection : &f11_intersection;

    while (inter->emergency_active && running)
        usleep(100000);

    PRINT("[PARKING] Vehicle #%d (%s) attempting to park at %s\n",
        v->id, v->type_name, inter_str(lot->location));

    if (sem_trywait(&lot->spots) == 0) {
        pthread_mutex_lock(&lot->lock);
        lot->available_spots--;
        int left = lot->available_spots;
        pthread_mutex_unlock(&lot->lock);
        PRINT("[PARKING] Vehicle #%d (%s) PARKED at %s  [spots left: %d]\n",
            v->id, v->type_name, inter_str(lot->location), left);

             pthread_mutex_lock(&stats_mutex);      
        vehicles_parked++;                     
        pthread_mutex_unlock(&stats_mutex);  

        int travel_time = 300000 + (rand() % 400000);
        for (int i = 0; i < travel_time / 10000 && running; i++)
            usleep(10000);
        return PARKED;
    }

    if (sem_trywait(&lot->queue) == 0) {
        v->park_status = IN_QUEUE;
        pthread_mutex_lock(&lot->lock);
        lot->waiting_count++;
        pthread_mutex_unlock(&lot->lock);

        while (running) {
            while (inter->emergency_active && running) usleep(100000);
            if (!running) break;
            if (sem_trywait(&lot->spots) == 0) break;
            usleep(100000);
        }

        pthread_mutex_lock(&lot->lock);
        lot->waiting_count--;
        lot->available_spots--;
        int left = lot->available_spots;
        pthread_mutex_unlock(&lot->lock);
        sem_post(&lot->queue);

        if (!running) return PARK_FAILED;

        PRINT("[PARKING] Vehicle #%d (%s) PARKED at %s  [spots left: %d]\n",
            v->id, v->type_name, inter_str(lot->location), left);
  pthread_mutex_lock(&stats_mutex);     
        vehicles_parked++;              
        pthread_mutex_unlock(&stats_mutex); 

        int travel_time = 300000 + (rand() % 400000);
        for (int i = 0; i < travel_time / 10000 && running; i++)
            usleep(10000);
        return PARKED;
    }

    PRINT("[PARKING] Vehicle #%d (%s) REJECTED at %s (full) – proceeding\n",
        v->id, v->type_name, inter_str(lot->location));
    return PARK_FAILED;
}

void parking_exit(Vehicle* v, ParkingLot* lot) {
    pthread_mutex_lock(&lot->lock);
    lot->available_spots++;
    int left = lot->available_spots;
    pthread_mutex_unlock(&lot->lock);
    sem_post(&lot->spots);
    PRINT("[PARKING] Vehicle #%d (%s) LEFT %s  [spots now: %d]\n",
        v->id, v->type_name, inter_str(lot->location), left);
}

void parking_destroy(ParkingLot* lot) {
    sem_destroy(&lot->spots);
    sem_destroy(&lot->queue);
    pthread_mutex_destroy(&lot->lock);
    PRINT("[CLEANUP] Parking lot %s destroyed\n", inter_str(lot->location));
}

//CONFLICT DETECTION 
static Direction straight_exit(Direction entry) {
    switch (entry) {
    case NORTH: return SOUTH; case SOUTH: return NORTH;
    case EAST:  return WEST;  default:    return EAST;
    }
}
static Direction right_exit(Direction entry) {
    switch (entry) {
    case NORTH: return WEST; case SOUTH: return EAST;
    case EAST:  return NORTH; default:  return SOUTH;
    }
}
static Direction left_exit(Direction entry) {
    switch (entry) {
    case NORTH: return EAST; case SOUTH: return WEST;
    case EAST:  return SOUTH; default:  return NORTH;
    }
}
static Direction exit_direction(Direction entry, Movement mov) {
    switch (mov) {
    case STRAIGHT: return straight_exit(entry);
    case RIGHT:    return right_exit(entry);
    default:       return left_exit(entry);
    }
}

int is_conflicting(Direction d1, Movement m1, Direction d2, Movement m2) {
    if (d1 == d2) return 1;
    Direction e1 = exit_direction(d1, m1);
    Direction e2 = exit_direction(d2, m2);
    if (e1 == e2) return 1;
    if (m1 == RIGHT || m2 == RIGHT) return 0;

    if (m1 == STRAIGHT && m2 == STRAIGHT) {
        if ((d1 == NORTH && d2 == SOUTH) || (d1 == SOUTH && d2 == NORTH)) return 0;
        if ((d1 == EAST && d2 == WEST) || (d1 == WEST && d2 == EAST))  return 0;
        return 1;
    }
    if (m1 == LEFT && m2 == STRAIGHT) {
        if ((d1 == NORTH && d2 == EAST) || (d1 == EAST && d2 == SOUTH) ||
            (d1 == SOUTH && d2 == WEST) || (d1 == WEST && d2 == NORTH)) return 1;
        return 0;
    }
    if (m2 == LEFT && m1 == STRAIGHT) {
        if ((d2 == NORTH && d1 == EAST) || (d2 == EAST && d1 == SOUTH) ||
            (d2 == SOUTH && d1 == WEST) || (d2 == WEST && d1 == NORTH)) return 1;
        return 0;
    }
    if (m1 == LEFT && m2 == LEFT) {
        if ((d1 == NORTH && d2 == SOUTH) || (d1 == SOUTH && d2 == NORTH) ||
            (d1 == EAST && d2 == WEST) || (d1 == WEST && d2 == EAST))  return 1;
        return 0;
    }
    return 0;
}

//INTERSECTION CONTROL  
void intersection_init(Intersection* inter, IntersectionID id) {
    inter->id = id;
    inter->light_state = GREEN_NS;
    inter->emergency_active = 0;
    pthread_mutex_init(&inter->lock, NULL);
    pthread_cond_init(&inter->clear_cond, NULL);
    memset(inter->vehicles_crossing, 0, sizeof(inter->vehicles_crossing));
    PRINT("[INIT] Intersection %s ready\n", inter_str(id));
}

int total_crossing(const Intersection* inter) {
    int n = 0;
    for (int d = 0; d < 4; d++)
        for (int m = 0; m < 3; m++)
            n += inter->vehicles_crossing[d][m];
    return n;
}

int can_cross(const Intersection* inter, const Vehicle* v) {
    if (is_emergency(v))        return 1;
    if (inter->emergency_active) return 0;

    if (v->origin_direction == NORTH || v->origin_direction == SOUTH) {
        if (inter->light_state != GREEN_NS) return 0;
    }
    else {
        if (inter->light_state != GREEN_EW) return 0;
    }

    for (int d = 0; d < 4; d++)
        for (int m = 0; m < 3; m++)
            if (inter->vehicles_crossing[d][m] > 0)
                if (is_conflicting(v->origin_direction, v->movement, (Direction)d, (Movement)m))
                    return 0;
    return 1;
}

void cross_intersection(Intersection* inter, Vehicle* v, Direction entry_dir, Movement mov) {
    pthread_mutex_lock(&inter->lock);

    if (is_emergency(v) && !inter->emergency_active) {
        inter->emergency_active = 1;
        inter->light_state = EMERGENCY_MODE;
        PRINT("[EMERGENCY] Vehicle #%d (%s) triggered EMERGENCY MODE at %s\n",
            v->id, v->type_name, inter_str(inter->id));

        while (total_crossing(inter) > 0 && running) {
            pthread_mutex_unlock(&inter->lock);
            usleep(50000);
            pthread_mutex_lock(&inter->lock);
        }
    }

    int first_wait = 1;
    while (!can_cross(inter, v) && running) {
        pthread_mutex_unlock(&inter->lock);
        if (first_wait) {
            PRINT("[WAIT] Vehicle #%d (%s) waiting at %s (signal red)\n",
                v->id, v->type_name, inter_str(inter->id));
            first_wait = 0;
        }
        usleep(100000);
        pthread_mutex_lock(&inter->lock);
    }

    if (!running) { pthread_mutex_unlock(&inter->lock); return; }

    inter->vehicles_crossing[entry_dir][mov]++;
    pthread_mutex_unlock(&inter->lock);

    PRINT("[CROSSING] Vehicle #%d (%s) [P%d] entering %s from %s going %s\n",
        v->id, v->type_name, v->priority,
        inter_str(inter->id), dir_str(entry_dir), mov_str(mov));

    int cross_time = 400000 + (rand() % 400000);
    for (int i = 0; i < cross_time / 10000 && running; i++)
        usleep(10000);

    pthread_mutex_lock(&inter->lock);
    inter->vehicles_crossing[entry_dir][mov]--;

    if (is_emergency(v) && inter->emergency_active) {
        inter->emergency_active = 0;
        inter->light_state = GREEN_NS;
        PRINT("[EMERGENCY] %s cleared — resuming normal operation\n", inter_str(inter->id));
        pthread_cond_broadcast(&inter->clear_cond);
    }
    pthread_mutex_unlock(&inter->lock);

    PRINT("[CROSSED]  Vehicle #%d (%s) cleared %s\n",
        v->id, v->type_name, inter_str(inter->id));
}


//TRAFFIC LIGHT THREAD  
void* light_cycle_thread(void* arg) {
    Intersection* inter = (Intersection*)arg;
    while (running) {
        if (inter->emergency_active) { usleep(200000); continue; }

        pthread_mutex_lock(&inter->lock);
        if (!inter->emergency_active) inter->light_state = GREEN_NS;
        pthread_mutex_unlock(&inter->lock);
        PRINT("[LIGHT] %s → GREEN North-South\n", inter_str(inter->id));

        for (int i = 0; i < 40 && running && !inter->emergency_active; i++)
            usleep(100000);

        if (inter->emergency_active) continue;

        pthread_mutex_lock(&inter->lock);
        if (!inter->emergency_active) inter->light_state = GREEN_EW;
        pthread_mutex_unlock(&inter->lock);
        PRINT("[LIGHT] %s → GREEN East-West\n", inter_str(inter->id));

        for (int i = 0; i < 40 && running && !inter->emergency_active; i++)
            usleep(100000);
    }
    return NULL;
}

//IPC HELPERS 
void send_emergency_alert(const Vehicle* v, int write_fd,
    IntersectionID from, IntersectionID to) {
    char msg[256];
    snprintf(msg, sizeof(msg), "EMERGENCY:%s#%d from %s",
        v->type_name, v->id, inter_str(from));
    PRINT("[PIPE] %s → %s controller: \"%s\"\n",
        inter_str(from), inter_str(to), msg);
    write(write_fd, msg, strlen(msg) + 1);
}

//CONTROLLER PROCESS
void run_controller(int read_fd, int write_fd, IntersectionID id) {
    printf("[CONTROLLER %s] Process started (PID: %d)\n", inter_str(id), getpid());
    fflush(stdout);

    char buf[256];
    while (1) {
        memset(buf, 0, sizeof(buf));
        ssize_t n = read(read_fd, buf, sizeof(buf) - 1);

        if (n > 0) {
            if (strncmp(buf, "TERMINATE", 9) == 0) {
                printf("[CONTROLLER %s] Received TERMINATE — shutting down\n", inter_str(id));
                fflush(stdout);
                break;
            }
            if (strncmp(buf, "EMERGENCY", 9) == 0) {
                printf("[CONTROLLER %s]  EMERGENCY ALERT: %s\n", inter_str(id), buf);
                printf("[CONTROLLER %s] Acknowledging — intersection will be cleared\n", inter_str(id));
                fflush(stdout);

                int vehicle_id = 0;
                char* p = strchr(buf, '#');
                if (p) vehicle_id = atoi(p + 1);

                usleep(500000);

                char ack[64];
                snprintf(ack, sizeof(ack), "ACK_EMERGENCY:%d", vehicle_id);
                write(write_fd, ack, strlen(ack) + 1);
            }
        }
        else if (n == 0) {
            break;
        }
        usleep(50000);
    }

    close(read_fd);
    close(write_fd);
    printf("[CONTROLLER %s] Exiting cleanly\n", inter_str(id));
    fflush(stdout);
    exit(0);
}


//ACK READER THREAD 
typedef struct { int f10_fd; int f11_fd; } AckFds;

void* ack_reader_thread(void* arg) {
    AckFds* fds = (AckFds*)arg;
    char buf[256];

    while (running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fds->f10_fd, &rfds);
        FD_SET(fds->f11_fd, &rfds);
        int maxfd = (fds->f10_fd > fds->f11_fd) ? fds->f10_fd : fds->f11_fd;
        struct timeval tv = { 0, 100000 };

        int ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ret <= 0) continue;

        if (FD_ISSET(fds->f10_fd, &rfds)) {
            memset(buf, 0, sizeof(buf));
            ssize_t n = read(fds->f10_fd, buf, sizeof(buf) - 1);
            if (n > 0 && strstr(buf, "ACK_EMERGENCY")) {
                pthread_mutex_lock(&cleared_mutex);
                f10_cleared = 1;
                pthread_mutex_unlock(&cleared_mutex);
                pthread_mutex_lock(&f10_intersection.lock);
                f10_intersection.emergency_active = 1;
                f10_intersection.light_state = EMERGENCY_MODE;
                pthread_mutex_unlock(&f10_intersection.lock);
            }
        }

        if (FD_ISSET(fds->f11_fd, &rfds)) {
            memset(buf, 0, sizeof(buf));
            ssize_t n = read(fds->f11_fd, buf, sizeof(buf) - 1);
            if (n > 0 && strstr(buf, "ACK_EMERGENCY")) {
                pthread_mutex_lock(&cleared_mutex);
                f11_cleared = 1;
                pthread_mutex_unlock(&cleared_mutex);
                pthread_mutex_lock(&f11_intersection.lock);
                f11_intersection.emergency_active = 1;
                f11_intersection.light_state = EMERGENCY_MODE;
                pthread_mutex_unlock(&f11_intersection.lock);
            }
        }
    }
    return NULL;
}


//SIGNAL HANDLER 
void sigint_handler(int sig) { (void)sig; running = 0; }


//ASSIGN PRIORITY
static int assign_priority(VehicleType t) {
    switch (t) {
    case AMBULANCE: return 0;
    case FIRETRUCK: return 0;
    case BUS:       return 2;
    case CAR:       return 3;
    case BIKE:      return 4;
    case TRACTOR:   return 5;
    default:        return 5;
    }
}

//USER INPUT
static void print_separator(void) {
    printf("-------------------------------------------------------\n");
}

//USER INPUT
static int read_int(const char* prompt, int lo, int hi) {
    int val;
    char line[64];
    while (1) {
        printf("%s", prompt);
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) {
            val = lo;
            break;
        }
        if (sscanf(line, "%d", &val) == 1 && val >= lo && val <= hi)
            break;
        printf("  Please enter a number between %d and %d.\n", lo, hi);
    }
    return val;
}

//print menus and collect all 15 vehicle configs
static void print_welcome(void) {
    printf("\n");
    printf("-----------------------------------------------------------\n");
    printf("-                   TRAFFIC SIMULATION                   -\n");
    printf("-                  OS Project Spring 2026                 -\n");
    printf("-----------------------------------------------------------\n");
    printf("   to test RACE CONDITIONS:   give two vehicles the\n");
    printf("         same intersection, same direction, spawn delay 0.\n");
    printf("   to test CONFLICTS:         give two vehicles the\n");
    printf("         same intersection, conflicting directions/moves.\n");
    printf("   to test EMERGENCY:         choose AMBULANCE or\n");
    printf("         FIRETRUCK — they clear the intersection instantly.\n");
    printf("   to test PARKING OVERFLOW:  set >10 vehicles to\n");
    printf("         wants_to_park=YES at the same intersection.\n");
    printf("\n");
}

static void print_vehicle_menu_header(int id) {
    printf("\n");
    print_separator();
    printf("  VEHICLE #%d / %d\n", id, TOTAL_VEHICLES);
    print_separator();
}

static void print_summary_table(Vehicle vehicles[], int n) {
    printf("\n");
    printf("==================================================================================\n");
    printf("|                        VEHICLE CONFIGURATION SUMMARY                           |\n");
    printf("==================================================================================\n");
    printf("| # | Type      | Prio | Origin  | From Dir  | Dest     | Move  | Park?  | Delay |\n");
    printf("==================================================================================\n");
    for (int i = 0; i < n; i++) {
        Vehicle* v = &vehicles[i];
        printf("|%2d | %-9s |  P%d  | %-7s | %-9s | %-8s | %-8s | %-6s |%4dms\n",
            v->id,
            v->type_name,
            v->priority,
            inter_str(v->origin_intersection),
            dir_str(v->origin_direction),
            inter_str(v->destination_intersection),
            mov_str(v->movement),
            v->wants_to_park ? "YES" : "NO",
            v->spawn_delay_ms);
    }
    printf("==================================================================================\n");
    printf("\n");
}

static void collect_user_input(Vehicle vehicles[]) {
    char filename[256];
    FILE* fp = NULL;

    printf("\n");
    printf("===========================================================\n");
    printf("|                   TRAFFIC SIMULATION                    |\n");
    printf("|                 OS Project Spring 2026                  |\n");
    printf("===========================================================\n");
    printf("\n");

    while (fp == NULL) {
        printf("  Enter input file name (e.g., vehicles.txt): ");
        fflush(stdout);
        if (fgets(filename, sizeof(filename), stdin) == NULL) {
            printf("  Error reading input. Exiting.\n");
            exit(1);
        }
        filename[strcspn(filename, "\n")] = 0;

        fp = fopen(filename, "r");
        if (fp == NULL) {
            printf("  Cannot open '%s'. Try again.\n", filename);
        }
    }

    printf("\n  Reading vehicles from '%s'...\n\n", filename);

    int count = 0;
    char line[256];

    while (count < TOTAL_VEHICLES && fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\n")] = 0;
        line[strcspn(line, "\r")] = 0;

        if (strlen(line) == 0) continue;

        int only_spaces = 1;
        for (int i = 0; line[i] != '\0'; i++) {
            if (line[i] != ' ' && line[i] != '\t') {
                only_spaces = 0;
                break;
            }
        }
        if (only_spaces) continue;

        if (line[0] == '#' || line[0] == '/') continue;

        Vehicle* v = &vehicles[count];
        memset(v, 0, sizeof(Vehicle));
        v->id = count + 1;
        v->park_status = NO_PARK;

        int t, orig, dir, mov, dest, park, delay;
        int num = sscanf(line, "%d %d %d %d %d %d %d",
                         &t, &orig, &dir, &mov, &dest, &park, &delay);

        if (num != 7) {
            printf("   Line %d: invalid format (got %d values, need 7), skipping.\n", count + 1, num);
            printf("     Line content: '%s'\n", line);
            continue;
        }

        if (t < 0 || t > 5)   { printf("   Line %d: invalid type %d, skipping.\n", count + 1, t); continue; }
        if (orig < 0 || orig > 1) { printf("   Line %d: invalid origin %d, skipping.\n", count + 1, orig); continue; }
        if (dir < 0 || dir > 3)   { printf("   Line %d: invalid direction %d, skipping.\n", count + 1, dir); continue; }
        if (mov < 0 || mov > 2)   { printf("   Line %d: invalid movement %d, skipping.\n", count + 1, mov); continue; }
        if (dest < 0 || dest > 1) { printf("   Line %d: invalid destination %d, skipping.\n", count + 1, dest); continue; }
        if (park < 0 || park > 1) { printf("   Line %d: invalid parking %d, skipping.\n", count + 1, park); continue; }
        if (delay < 0 || delay > 5000) { printf("   Line %d: invalid delay %d, skipping.\n", count + 1, delay); continue; }

        v->type = (VehicleType)t;
        strncpy(v->type_name, type_name(v->type), MAX_VEHICLE_NAME - 1);
        v->priority = assign_priority(v->type);
        v->origin_intersection = (IntersectionID)orig;
        v->origin_direction = (Direction)dir;
        v->movement = (Movement)mov;
        v->destination_intersection = (IntersectionID)dest;

        if (is_emergency(v))
            v->wants_to_park = 0;
        else
            v->wants_to_park = park;

        v->spawn_delay_ms = (unsigned int)delay;

        printf("  Vehicle #%d: %-9s | %s/%s | %-8s | → %s | Park:%s | Delay:%dms\n",
               v->id, v->type_name,
               inter_str(v->origin_intersection), dir_str(v->origin_direction),
               mov_str(v->movement), inter_str(v->destination_intersection),
               v->wants_to_park ? "YES" : "NO", v->spawn_delay_ms);

        count++;
    }

    fclose(fp);

    if (count == 0) {
        printf("\n  ERROR: No valid vehicles found in file.\n");
        printf("  Check that the file has lines like: 3 0 0 0 1 0 500\n");
        exit(1);
    }

    if (count < TOTAL_VEHICLES) {
        printf("\n  ERROR: Only %d vehicles found (need exactly %d).\n", count, TOTAL_VEHICLES);
        printf("  Add %d more vehicle(s) to the file.\n", TOTAL_VEHICLES - count);
        exit(1);
    }

    /* Show summary table */
    printf("\n");
    printf("=============================================================================================\n");
    printf("|                        VEHICLE CONFIGURATION SUMMARY                                      |\n");
    printf("=============================================================================================\n");
    printf("| # | Type      | Prio | Origin  | From Dir  | Dest       | Move       | Park?   | Dlay     |\n");
    printf("=============================================================================================\n");
    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        Vehicle* v = &vehicles[i];
        printf("|%2d | %-9s |  P%d  | %-7s | %-9s | %-8s  | %-5s   | %-6s    |%4dms    |\n",
               v->id, v->type_name, v->priority,
               inter_str(v->origin_intersection), dir_str(v->origin_direction),
               inter_str(v->destination_intersection), mov_str(v->movement),
               v->wants_to_park ? "YES" : "NO", v->spawn_delay_ms);
    }
    printf("=============================================================================================\n");
    printf("\n");

    printf("  Press ENTER to start the simulation...");
    fflush(stdout);
    char dummy[8];
    fgets(dummy, sizeof(dummy), stdin);
}

//VEHICLE THREAD 
void* vehicle_thread(void* arg) {
     Vehicle* v = (Vehicle*)arg;
    
    if (v->spawn_delay_ms > 0) {
        for (unsigned int i = 0; i < v->spawn_delay_ms && running; i++)
            usleep(1000);
    }
    
    if (!running) {
        PRINT("[CLEANUP] Vehicle #%d aborted during spawn delay\n", v->id);
        return NULL;
    }
    if (!running) return NULL;

    PRINT("\n[SPAWN] Vehicle #%d | %-9s | Priority:%d | Origin:%s/%s | Move:%s | Park:%s\n",
        v->id, v->type_name, v->priority,
        inter_str(v->origin_intersection), dir_str(v->origin_direction),
        mov_str(v->movement), v->wants_to_park ? "YES" : "NO");

    // PARKING
    if (v->wants_to_park && !is_emergency(v)) {
        ParkingLot* lot = (v->origin_intersection == F10) ? &f10_parking : &f11_parking;
        v->park_status = parking_enter(v, lot);
        if (v->park_status == PARKED)
            parking_exit(v, lot);
    }

    // EMERGENCY IPC
    if (is_emergency(v)) {
        if (v->destination_intersection != v->origin_intersection) {
            int alert_fd;
            volatile int* cleared_flag;
            IntersectionID dest = v->destination_intersection;

            if (dest == F11) {
                alert_fd = pipe_to_f11[1];
                cleared_flag = &f11_cleared;
            }
            else {
                alert_fd = pipe_to_f10[1];
                cleared_flag = &f10_cleared;
            }

            pthread_mutex_lock(&cleared_mutex);
            *cleared_flag = 0;
            pthread_mutex_unlock(&cleared_mutex);

            send_emergency_alert(v, alert_fd, v->origin_intersection, dest);

              pthread_mutex_lock(&stats_mutex);      
            emergency_alerts++;                    
            pthread_mutex_unlock(&stats_mutex);    

            int waited_ms = 0;
            int timeout_occurred = 0;
            while (!(*cleared_flag) && running && waited_ms < 5000) {
                usleep(100000);
                waited_ms += 100;
            }

            if (waited_ms >= 5000) {
                timeout_occurred = 1;
                PRINT("[WARNING] Emergency #%d timeout waiting for ACK at %s (waited 5s)\n",
                    v->id, inter_str(v->destination_intersection));
            }
        }
    }

    // CROSS ORIGIN INTERSECTION 
    Intersection* origin_inter =
        (v->origin_intersection == F10) ? &f10_intersection : &f11_intersection;
    cross_intersection(origin_inter, v, v->origin_direction, v->movement);

    pthread_mutex_lock(&stats_mutex);
    vehicles_crossed++;
    
    pthread_mutex_unlock(&stats_mutex);

    //TRAVEL TO DESTINATION
    if (v->destination_intersection != v->origin_intersection) {
        Direction exit_dir = get_exit_direction(v->origin_direction, v->movement);
        Direction arrival_dir = get_arrival_direction(exit_dir);

        PRINT("[TRAVEL] Vehicle #%d (%s) traveling %s → %s (exit %s, arrive %s)\n",
            v->id, v->type_name,
            inter_str(v->origin_intersection), inter_str(v->destination_intersection),
            dir_str(exit_dir), dir_str(arrival_dir));

           int travel_time = 300000 + (rand() % 400000);
        for (int i = 0; i < travel_time / 10000 && running; i++)
            usleep(10000);

        Intersection* dest_inter =
            (v->destination_intersection == F10) ? &f10_intersection : &f11_intersection;
        cross_intersection(dest_inter, v, arrival_dir, v->movement);
    }

    PRINT("[DONE]   Vehicle #%d (%s) reached destination \n", v->id, v->type_name);
    return NULL;
}

//MAIN
int main(void) {
    srand((unsigned int)time(NULL));

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, sigint_handler);

    //Collect all vehicle configs from the user
    Vehicle vehicles[TOTAL_VEHICLES];
    collect_user_input(vehicles);

    //Initialize simulation infrastructure 
    printf("============================================================\n");
    printf("|                   SIMULATION STARTING                    |\n");
    printf("============================================================\n\n");

    intersection_init(&f10_intersection, F10);
    intersection_init(&f11_intersection, F11);
    parking_init(&f10_parking, F10);
    parking_init(&f11_parking, F11);

    if (pipe(pipe_to_f10) < 0 || pipe(pipe_to_f11) < 0 ||
        pipe(pipe_from_f10) < 0 || pipe(pipe_from_f11) < 0) {
        perror("pipe");
        return 1;
    }
    PRINT("[SETUP] IPC pipes created\n");
    PRINT("[SETUP] SIGINT handler registered\n");

    //Fork controller processes
    f10_pid = fork();
    if (f10_pid < 0) { perror("fork F10"); return 1; }
    if (f10_pid == 0) {
        close(pipe_to_f10[1]);  close(pipe_from_f10[0]);
        close(pipe_to_f11[0]);  close(pipe_to_f11[1]);
        close(pipe_from_f11[0]); close(pipe_from_f11[1]);
        run_controller(pipe_to_f10[0], pipe_from_f10[1], F10);
    }

    f11_pid = fork();
    if (f11_pid < 0) { perror("fork F11"); return 1; }
    if (f11_pid == 0) {
        close(pipe_to_f11[1]);  close(pipe_from_f11[0]);
        close(pipe_to_f10[0]);  close(pipe_to_f10[1]);
        close(pipe_from_f10[0]); close(pipe_from_f10[1]);
        run_controller(pipe_to_f11[0], pipe_from_f11[1], F11);
    }

    close(pipe_to_f10[0]);  close(pipe_to_f11[0]);
    close(pipe_from_f10[1]); close(pipe_from_f11[1]);

    PRINT("[SETUP] Controller processes: F10(PID:%d) F11(PID:%d)\n", f10_pid, f11_pid);
    sleep(1);

    //Start background threads
    pthread_t t_ack, t_light_f10, t_light_f11;
    AckFds ack_fds = { pipe_from_f10[0], pipe_from_f11[0] };
    pthread_create(&t_ack, NULL, ack_reader_thread, &ack_fds);
    pthread_create(&t_light_f10, NULL, light_cycle_thread, &f10_intersection);
    pthread_create(&t_light_f11, NULL, light_cycle_thread, &f11_intersection);

    //Spawn all vehicle threads simultaneously
    pthread_t threads[TOTAL_VEHICLES];

    printf("\n=========== LAUNCHING ALL %d VEHICLE THREADS ==============\n\n",
        TOTAL_VEHICLES);
    printf("  (Each vehicle's spawn_delay_ms controls when it actually\n");
    printf("   enters the simulation after the thread is created.)\n\n");

    for (int i = 0; i < TOTAL_VEHICLES && running; i++) {
        pthread_create(&threads[i], NULL, vehicle_thread, &vehicles[i]);
    }

    printf("\n=============== ALL %d THREADS CREATED — SIMULATION RUNNING =============\n\n",
        TOTAL_VEHICLES);

    /* ── Wait for all vehicle threads to finish ── */
    for (int i = 0; i < TOTAL_VEHICLES; i++)
        pthread_join(threads[i], NULL);

    printf("\n============ ALL VEHICLES COMPLETED ================\n\n");

    printf("=========================================\n");
    printf("|           FINAL STATISTICS            |\n");
    printf("=========================================\n");
    printf("|  Vehicles crossed intersections: %2d   |\n", vehicles_crossed);
    printf("|  Vehicles parked:               %2d    |\n", vehicles_parked);
    printf("|  Emergency alerts sent:         %2d    |\n", emergency_alerts);
    printf("=========================================\n\n");

    //Shutdown
    running = 0;

    pthread_join(t_light_f10, NULL);
    pthread_join(t_light_f11, NULL);
    pthread_join(t_ack, NULL);

    const char* term = "TERMINATE";
    write(pipe_to_f10[1], term, strlen(term) + 1);
    write(pipe_to_f11[1], term, strlen(term) + 1);
    close(pipe_to_f10[1]);
    close(pipe_to_f11[1]);

    waitpid(f10_pid, NULL, 0);
    waitpid(f11_pid, NULL, 0);
    PRINT("[CLEANUP] Controller processes exited\n");

    parking_destroy(&f10_parking);
    parking_destroy(&f11_parking);

    pthread_mutex_destroy(&f10_intersection.lock);
    pthread_cond_destroy(&f10_intersection.clear_cond);
    pthread_mutex_destroy(&f11_intersection.lock);
    pthread_cond_destroy(&f11_intersection.clear_cond);
    pthread_mutex_destroy(&print_mutex);
    pthread_mutex_destroy(&cleared_mutex);
    pthread_mutex_destroy(&stats_mutex);

    close(pipe_from_f10[0]);
    close(pipe_from_f11[0]);

    printf("\n============================================================\n");
    printf("|       SIMULATION COMPLETED SUCCESSFULLY                  |\n");
    printf("============================================================\n");

    return 0;
}
