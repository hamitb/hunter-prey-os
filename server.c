#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdint.h>
#include "structs.h"

#define PIPE(fd) socketpair(AF_UNIX, SOCK_STREAM, 0, fd)

server_message get_state(uint16_t *map, actor_t a, int x, int y, int map_width, int map_height);
void move_actor(uint16_t *map, int x, int y, int new_x, int new_y, actor_t a, int map_width);

int get1D(int x, int y, int width) { return y * width + x; }

uint16_t encode_actor(actor_t a, int index) {
    uint16_t encd = a | (index << 3);

    return encd;
}

actor_t decode_actor(uint16_t encd) {
    return encd & 0x7;
}

uint16_t decode_index(uint16_t encd) {
    return encd >> 3;
}

void print_map(uint16_t *map, int map_width, int map_height) {
    int i, j;
    
    // Print top numbers
    /* printf(" +");
    for (i = 0; i < map_width; ++i) { printf("%d", i); }
    printf("+\n"); */

    // Print top dashes
    printf("+");
    for (i = 0; i < map_width; ++i) { printf("-"); }
    printf("+\n");

    // Print hunters, preys, obstacles
    for (i = 0; i < map_height; i++) {
        // printf("%d|", i);
        printf("|");
        for (j = 0; j < map_width; j++) {
            switch(decode_actor(map[get1D(j, i, map_width)])) {
                case HUNTER:
                    printf("H");
                    break;
                case PREY:
                    printf("P");
                    break;
                case OBSTACLE:
                    printf("X");
                    break;
                case DOUBLE:
                    printf("D");
                    break;
                case EMPTY:
                    printf(" ");
                    break;
            }
        }
        printf("|\n");
    }

    // Print bottom dashes
    printf("+");
    for (i = 0; i < map_width; ++i) { printf("-"); }
    printf("+\n");
}

void initialize_map(uint16_t *map, int map_width, Hunter *hunters, int hunter_count,
                Prey *preys, int prey_count) {
    int i;

    for (i = 0; i < hunter_count; ++i) {
        if (hunters[i].alive) {
            map[get1D(hunters[i].pos.x, hunters[i].pos.y, map_width)] = encode_actor(HUNTER, i);
        }
    }

    for (i = 0; i < prey_count; ++i) {
        if (preys[i].alive) {
            map[get1D(preys[i].pos.x, preys[i].pos.y, map_width)] = encode_actor(PREY, i);
        }
    }
}

void update_map(uint16_t *map, int map_width, Hunter *hunters, int hunter_count,
                Prey *preys, int prey_count, int *alive_prey_count, int *alive_hunter_count,
                int h_pipes[][2], int p_pipes[][2], struct pollfd *pfd_h, struct pollfd *pfd_p) {
    int i;
    uint16_t curr_encd, kill_prey_idx;

    for (i = 0; i < hunter_count; ++i) {
        if (hunters[i].alive) {
            curr_encd = map[get1D(hunters[i].pos.x, hunters[i].pos.y, map_width)];
            if (decode_actor(curr_encd) == DOUBLE) { /* Hunter kills prey */
                // Killed prey index
                kill_prey_idx = curr_encd >> 3;
                // Transfer its energy to the hunter and set it dead
                preys[kill_prey_idx].alive = 0;
                hunters[i].energy += preys[kill_prey_idx].stored_energy;
                // Close corresponding pipe
                close(p_pipes[kill_prey_idx][0]);
                // Send SIGTERM to the corresponding pid
                kill(preys[kill_prey_idx].pid, SIGTERM);
                // Reap
                waitpid(preys[kill_prey_idx].pid, NULL, 0);
                // Decrease alive prey count
                (*alive_prey_count)--;
                // Set revent flag of prey in pfd
                pfd_p[kill_prey_idx].fd = -1;
                // printf("KILL THE PREY AT %d (%d, %d)\n", kill_prey_idx, preys[kill_prey_idx].pos.x, preys[kill_prey_idx].pos.y);
            }
            // Check if hunter is dead
            if (hunters[i].energy <= 0) {
                // Kill hunter
                hunters[i].alive = 0;
                // Close corresponding pipe
                close(h_pipes[i][0]);
                // Send SIGTERM to the corresponding pid
                kill(hunters[i].pid, SIGTERM);
                // Reap
                waitpid(hunters[i].pid, NULL, 0);
                // Decrease alive hunter count
                (*alive_hunter_count)--;
                // Set revent flag of hunter in pfd
                pfd_h[i].fd = -1;
            }
            // Update map
            if (hunters[i].alive) {
                map[get1D(hunters[i].pos.x, hunters[i].pos.y, map_width)] = encode_actor(HUNTER, i);
            } else {
                map[get1D(hunters[i].pos.x, hunters[i].pos.y, map_width)] = EMPTY;
            }
        }
    }
}

void setup_children(Hunter *hunters, int hunter_count, int h_pipes[][2], Prey *preys,
                    int prey_count, int p_pipes[][2], uint16_t *map,
                    int map_width, int map_height) {
    int i, j;
    pid_t pid;
    server_message state;

    // Create hunter pipes
    for (i = 0; i < hunter_count; ++i) {
        if (PIPE(h_pipes[i]) < 0) {
            perror("Hunter pipe creation error");
            exit(1);
        }
    }

    // Fork hunters and close unnecessary pipes
    for (i = 0; i < hunter_count; ++i) {
        if ((pid = fork()) < 0) {
            perror("Hunter fork error");
        } else if (pid > 0) { /* Server */
            // Close child end
            hunters[i].pid = pid;
        } else { /* New Hunter Process*/
            // Close parent end
            close(h_pipes[i][0]);
            // Close other hunters' pipes
            for (j = 0; j < hunter_count; ++j) {
                if (j != i) {
                    close(h_pipes[j][1]);
                    close(h_pipes[j][0]);
                }
            }
            
            // Redirect stdin and stdout to pipe
            dup2(h_pipes[i][1], 1);
            dup2(h_pipes[i][1], 0);
    
            // Execute hunter process
            char m_width[10];
            char m_height[10];

            sprintf(m_width, "%d", map_width);
            sprintf(m_height, "%d", map_height);

            execl("./hunter", "hunter", m_width, m_height, NULL);
        }
    }
    
    // Create prey pipes
    for (i = 0; i < prey_count; ++i) {
        if (PIPE(p_pipes[i]) < 0) {
            perror("Prey pipe creation error");
            exit(1);
        }
    }

    // Fork preys and close unnecessary pipes
    for (i = 0; i < prey_count; ++i) {
        if ((pid = fork()) < 0) {
            perror("Prey fork error");
        } else if (pid > 0) { /* Server */
            // Close child end
            preys[i].pid = pid;
        } else { /* New Hunter Process*/
            // Close parent end
            close(p_pipes[i][0]);

            // Close other preys' pipes
            for (j = 0; j < prey_count; ++j) {
                if (j != i) {
                    close(p_pipes[j][1]);
                    close(p_pipes[j][0]);
                }
            }

            // Close other hunters' pipes
            for (j = 0; j < hunter_count; ++j) {
                close(h_pipes[j][0]);
                close(h_pipes[j][1]);
            }

            // Redirect stdin and stdout to pipe
            dup2(p_pipes[i][1], 1);
            dup2(p_pipes[i][1], 0);
    
            // Execute hunter process
            char m_width[10];
            char m_height[10];

            sprintf(m_width, "%d", map_width);
            sprintf(m_height, "%d", map_height);

            execl("./prey", "prey", m_width, m_height, NULL);
        }
    }

    for (i = 0; i < hunter_count; ++i) {
        close(h_pipes[i][1]);
    }

    for (i = 0; i < prey_count; ++i) {
        close(p_pipes[i][1]);
    }


    for (i = 0; i < hunter_count; ++i) {
        state = get_state(map, HUNTER, hunters[i].pos.x, hunters[i].pos.y, map_width, map_height);
        write(h_pipes[i][0], &state, sizeof(server_message));
    }

    for (i = 0; i < prey_count; ++i) {
        state = get_state(map, PREY, preys[i].pos.x, preys[i].pos.y, map_width, map_height);
        write(p_pipes[i][0], &state, sizeof(server_message));
    }
}

server_message get_state(uint16_t *map, actor_t a, int x, int y, int map_width, int map_height) {
    
    int i, j, k, l, offset_x, offset_y;
    uint8_t adv_found;
    int coeffs[2] = {-1, 1};
    server_message state;

    adv_found = 0;

    // Set position of state
    state.pos = (coordinate) {
        .x = x,
        .y = y,
    };

    // Found closest adversary
    for (i = 1; i < (map_height+map_width-2); ++i) {
        for (j = 0; j <= i; ++j) {
            for (k = 0; k < 2; ++k) {
                for (l = 0; l < 2; ++l) {
                    if ((x + coeffs[k]*j >= 0) && (y + coeffs[l]*(i-j) >= 0) && 
                        ((x + coeffs[k]*j) < map_width) && ((y + coeffs[l]*(i-j)) < map_height)) {
                        // Coordinates are valid now check if a adversary exist in that location
                        if ((a == HUNTER && (decode_actor(map[get1D(x+coeffs[k]*j, y+coeffs[l]*(i-j), map_width)]) == PREY))
                        || (a == HUNTER && (decode_actor(map[get1D(x+coeffs[k]*j, y+coeffs[l]*(i-j), map_width)]) == DOUBLE))
                        || (a == PREY && (decode_actor(map[get1D(x+coeffs[k]*j, y+coeffs[l]*(i-j), map_width)]) == HUNTER))
                        || (a == PREY && (decode_actor(map[get1D(x+coeffs[k]*j, y+coeffs[l]*(i-j), map_width)]) == DOUBLE))) {
                            adv_found = 1;
                            state.adv_pos.x = x+coeffs[k]*j;
                            state.adv_pos.y = y+coeffs[l]*(i-j);
                            break;
                        }
                    }
                }
                if (adv_found){
                   break;
                }
            }
            if (adv_found){
                break;
            }
        }
        if (adv_found){
            break;
        }
    }
    // Find neighbours
    // Reset neighbour count
    state.object_count = 0;

    for (i = -1; i < 2; ++i) {
        for (j = -1; j < 2; ++j) {
            offset_x = i;
            offset_y = j;

            if (offset_x == 0 && offset_y == 0) {
                continue;
            } else if (offset_x != 0) {
                offset_y = 0;
            }

            if ((x + offset_x) >= 0 && (y + offset_y) >= 0
            && ((x + offset_x) < map_width) && ((y + offset_y) < map_height)) {
                // Coordinates are valid, now check if a adversary exist in that location
                if (map[get1D(x+offset_x, y+offset_y, map_width)] == EMPTY) {
                    // If corresponding location is empty continue 
                    continue;
                } else if ((a == HUNTER && (decode_actor(map[get1D(x+offset_x, y+offset_y, map_width)]) != PREY))
                       || (a == PREY && (decode_actor(map[get1D(x+offset_x, y+offset_y, map_width)]) != HUNTER))) {
                    state.object_pos[state.object_count] = (coordinate){
                        .x = x + offset_x,
                        .y = y + offset_y,
                    };
                    state.object_count += 1;
                    break;
                }
            }
        }
    }

    return state;
}

uint8_t handle_request(ph_message request, uint16_t *map, Hunter *hunters, 
                        Prey *preys, actor_t a, int index, int map_width) {
    
    uint16_t requested_location = map[get1D(request.move_request.x, request.move_request.y, map_width)];
    uint8_t accepted = 0;

    switch (decode_actor(requested_location)) {
        case HUNTER:
            if (a == PREY) {
                accepted = 1;
            }
            break;
        case PREY:
            if (a == HUNTER) {
                accepted = 1;
            }
            break;
        case OBSTACLE:
            accepted = 0;
            break;
        case EMPTY:
            accepted = 1;
            break;
        default:
            break;
    }

    if (accepted && a == HUNTER) {
        move_actor(map, hunters[index].pos.x, hunters[index].pos.y, request.move_request.x, request.move_request.y, HUNTER, map_width);
        hunters[index].pos = request.move_request;
        // -1 Energy
        hunters[index].energy--;
    } else if (accepted && a == PREY) {
        move_actor(map, preys[index].pos.x, preys[index].pos.y, request.move_request.x, request.move_request.y, PREY, map_width);
        preys[index].pos = request.move_request;
    }

    return accepted;
}

void move_actor(uint16_t *map, int x, int y, int new_x, int new_y, actor_t a, int map_width) {
    if (a == EMPTY) {
        map[get1D(new_x, new_y, map_width)] = EMPTY;
    } else {
        // Get encoded value from old location
        uint16_t old_encd = map[get1D(x, y, map_width)];
        uint16_t new_encd = map[get1D(new_x, new_y, map_width)];

        

        // Put encoded value to the new location
        if (a == HUNTER) {
            if (decode_actor(new_encd) == PREY) {
                map[get1D(new_x, new_y, map_width)] = encode_actor(DOUBLE, decode_index(new_encd));
            } else if (decode_actor(new_encd) == DOUBLE) {
                return;
            } else {
                map[get1D(new_x, new_y, map_width)] = old_encd;
            }
            // Empty old location
            map[get1D(x, y, map_width)] = EMPTY;
        } else if (a == PREY) {
            if (decode_actor(new_encd) == HUNTER) {
                map[get1D(new_x, new_y, map_width)] = encode_actor(DOUBLE, decode_index(old_encd));
            } else {
                map[get1D(new_x, new_y, map_width)] = encode_actor(PREY, decode_index(old_encd));
            }

            if(decode_actor(old_encd) == DOUBLE) {
                map[get1D(x, y, map_width)] = HUNTER;
            } else {
                map[get1D(x, y, map_width)] = EMPTY;
            }
        }
    }
}

void kill_remaining(Hunter *hunters, Prey *preys, int hunter_count, int prey_count,
                    int alive_hunter_count, int alive_prey_count, int h_pipes[][2], int p_pipes[][2]) {
    int i;
    
    // If Hunters won
    if (alive_hunter_count > 0) {
        for (i = 0; i < hunter_count; ++i) {
            if (hunters[i].alive) {
                // Close corresponding pipe
                close(h_pipes[i][0]);
                // Kill corresponding process
                kill(hunters[i].pid, SIGTERM);
                // Reap it
                waitpid(hunters[i].pid, NULL, 0);
            }
        }
    } else if (alive_prey_count > 0) {
        for (i = 0; i < prey_count; ++i) {
            if (preys[i].alive) {
                // Close corresponding pipe
                close(p_pipes[i][0]);
                // Kill corresponding process
                kill(preys[i].pid, SIGTERM);
                // Reap it
                waitpid(preys[i].pid, NULL, 0);
            }
        }
    }
}

int main(int argc, char **argv) {
    // Declare variables
    int map_width, map_height, obs_count, i;
    int hunter_count, prey_count;
    int alive_hunter_count, alive_prey_count;
    
    // Input map, hunter, prey and obs details
    scanf("%d %d", &map_width, &map_height);
    scanf("%d", &obs_count);

    // Declare map
    uint16_t map[map_height*map_width];
    memset(map, 0, sizeof map);

    // Read obstacles
    Obstacle obs[obs_count];

    for (i = 0; i < obs_count; ++i) {
        scanf("%d %d", &(obs[i].pos.y), &(obs[i].pos.x));
        map[get1D(obs[i].pos.x, obs[i].pos.y, map_width)] = OBSTACLE;
    }

    // Read hunters
    scanf("%d", &hunter_count);

    Hunter hunters[hunter_count];
    alive_hunter_count = hunter_count;

    for (i = 0; i < hunter_count; ++i){
        scanf("%d %d %d", 
            &(hunters[i].pos.y), &(hunters[i].pos.x), &(hunters[i].energy));
        hunters[i].alive = 1;
    }

    // Read preys
    scanf("%d", &prey_count);

    Prey preys[prey_count];
    alive_prey_count = prey_count;

    for (i = 0; i < prey_count; ++i) {
        scanf("%d %d %d", 
            &(preys[i].pos.y), &(preys[i].pos.x), &(preys[i].stored_energy));
        preys[i].alive = 1;
    }

    // Declare pipes
    int h_pipes[hunter_count][2];
    int p_pipes[prey_count][2];

    // Initialize map with hunters' and preys' locations
    initialize_map(map, map_width, hunters, hunter_count, preys, prey_count);

    // Print initial map
    print_map(map, map_width, map_height);

    // Setup children processes and communication
    setup_children(hunters, hunter_count, h_pipes, preys, prey_count,
                    p_pipes, map, map_width, map_height);

    //printf("Server: All processes created successfully\n");

    // Declare pollfd
    uint8_t map_updated = 0;
    struct pollfd pfd_h[hunter_count];
    struct pollfd pfd_p[prey_count];
    server_message state;
    ph_message request;
    actor_t actor_type;

    // Setup pfds
    for (i = 0; i < hunter_count; ++i) {
        pfd_h[i].fd = h_pipes[i][0];
        pfd_h[i].events = POLLIN;
        pfd_h[i].revents = 0;
    }

    for (i = 0; i < prey_count; ++i) {
        pfd_p[i].fd = p_pipes[i][0];
        pfd_p[i].events = POLLIN;
        pfd_p[i].revents = 0;
    }

    // Main loop
    while (alive_prey_count > 0 && alive_hunter_count > 0) {
        // Reset revents
        for (i = 0; i < hunter_count; ++i) {
            if (pfd_h[i].fd >= 0) {
                pfd_h[i].revents = 0;
            }
        }

        for (i = 0; i < prey_count; ++i) {
            if (pfd_p[i].fd >= 0) {
                pfd_p[i].revents = 0;
            }
        }

        // Poll for hunters - 1
        poll(pfd_h, hunter_count, 0);
        // For hunters that ready process their request - 2
        for (i = 0; i < hunter_count; ++i) {
            if (pfd_h[i].revents && POLLIN) {
                // pid_t pid;
                // Read request - 2a
                read(pfd_h[i].fd, &request, sizeof(ph_message));
                // Print request
                /* printf("Request from HUNTER(%d)(%d), from %d,%d to %d,%d\n", hunters[i].pid,hunters[i].energy, 
                        hunters[i].pos.x, hunters[i].pos.y, request.move_request.x, request.move_request.y); */
                // Handle request - 2b 2c
                map_updated |= handle_request(request, map, hunters, preys, HUNTER, i, map_width);
                // Create new state for current actor - 2d
                state = get_state(map, HUNTER, hunters[i].pos.x, hunters[i].pos.y, map_width, map_height);
                // pid = hunters[i].pid;
                // Send new state
                // printf("Send request coming from fd:(%d) to process(%d)\n", pfd_h[i].fd, pid);
                write(pfd_h[i].fd, &state, sizeof(server_message));
            }
        }
        
        

        // Poll for preys - 1
        poll(pfd_p, prey_count, 0);
        // For hunters that ready process their request - 2
        for (i = 0; i < prey_count; ++i) {
            if (pfd_p[i].revents && POLLIN) {
                // pid_t pid;
                // Read request - 2a
                read(pfd_p[i].fd, &request, sizeof(ph_message));
                // Print request
                /* printf("Request from PREY(%d), from %d,%d to %d,%d\n", preys[i].pid, preys[i].pos.x, 
                        preys[i].pos.y, request.move_request.x, request.move_request.y); */
                // Handle request - 2b 2c
                map_updated |= handle_request(request, map, hunters, preys, PREY, i, map_width);
                // Create new state for current actor - 2d
                state = get_state(map, PREY, preys[i].pos.x, preys[i].pos.y, map_width, map_height);
                // pid = preys[i].pid;
                // Send new state
                // printf("Send request coming from fd:(%d) to process(%d)\n", pfd_p[i].fd, pid);
                write(pfd_p[i].fd, &state, sizeof(server_message));
            }
        }

        if (map_updated) {
            update_map(map, map_width, hunters, hunter_count, preys, prey_count, 
                        &alive_prey_count, &alive_hunter_count, h_pipes, p_pipes, pfd_h, pfd_p);
            print_map(map, map_width, map_height);
        }

        map_updated = 0;
    }

    kill_remaining(hunters, preys, hunter_count, prey_count, 
                    alive_hunter_count, alive_prey_count, h_pipes, p_pipes);

    exit(0);
}