#ifndef S_H
#define S_H

typedef enum actor {
    EMPTY = 0,
    OBSTACLE = 1,
    PREY = 2,
    HUNTER = 4,
    DOUBLE  = 6,
} actor_t;

typedef struct coordinate {
    int x;
    int y;
} coordinate;

typedef struct server_message {
    coordinate pos;
    coordinate adv_pos;
    int object_count;
    coordinate object_pos[4];
} server_message;

typedef struct ph_message {
    coordinate move_request;
} ph_message;

typedef struct Obstacle {
    coordinate pos;
} Obstacle;

typedef struct Hunter {
    coordinate pos;
    int energy;
    short alive;
    pid_t pid;
} Hunter;

typedef struct Prey {
    coordinate pos;
    int stored_energy;
    short alive;
    pid_t pid;
} Prey;

#endif