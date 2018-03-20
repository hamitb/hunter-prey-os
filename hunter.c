#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdint.h>
#include "structs.h"

int manhattan_dist(coordinate pos1, coordinate pos2) {
    return abs(pos1.x - pos2.x) + abs(pos1.y - pos2.y);
}

ph_message get_possible_move(server_message curr_state, int map_width, int map_height) {
    int i, candidate_dist;
    int available_dirs[4] = {1, 1, 1, 1};
    coordinate candidate_pos;
    coordinate curr_pos = curr_state.pos;
    coordinate adv_pos = curr_state.adv_pos;
    int min_dist = manhattan_dist(curr_pos, adv_pos);
    coordinate min_pos = curr_pos;

    ph_message request;

    // Mark unavailable directions

    if (curr_pos.x - 1 < 0) {
        available_dirs[3] = 0;
    }
    if (curr_pos.x + 1 >= map_width) {
        available_dirs[1] = 0;
    }
    if (curr_pos.y - 1 < 0) {
        available_dirs[0] = 0;
    }
    if (curr_pos.y + 1 >= map_height) {
        available_dirs[2] = 0;
    }

    for (i = 0; i < curr_state.object_count; ++i) {
        if (curr_state.object_pos[i].x == curr_pos.x) {
            if (curr_state.object_pos[i].y < curr_pos.y) {
                available_dirs[0] = 0;
                
            } else if(curr_state.object_pos[i].y > curr_pos.y) {
                available_dirs[2] = 0;
            }
        } else if (curr_state.object_pos[i].y == curr_pos.y) {
            if (curr_state.object_pos[i].x < curr_pos.x) {
                available_dirs[3] = 0;
            } else if(curr_state.object_pos[i].x > curr_pos.x) {
                available_dirs[1] = 0;
            }
        }
    }

    

    // Choose from available directions
    for (i = 0; i < 4; ++i) {
        if (available_dirs[i] != 0) {
            switch (i) {
                case 0:
                    candidate_pos.x = curr_pos.x;
                    candidate_pos.y = curr_pos.y - 1;
                    candidate_dist = manhattan_dist(adv_pos, candidate_pos);
                    if (candidate_dist < min_dist) {
                        min_dist = candidate_dist;
                        min_pos = candidate_pos;
                    }
                    break;
                case 1:
                    candidate_pos.x = curr_pos.x + 1;
                    candidate_pos.y = curr_pos.y;
                    candidate_dist = manhattan_dist(adv_pos, candidate_pos);
                    if (candidate_dist < min_dist) {
                        min_dist = candidate_dist;
                        min_pos = candidate_pos;
                    }   
                    break;
                case 2:
                    candidate_pos.x = curr_pos.x;
                    candidate_pos.y = curr_pos.y + 1;
                    candidate_dist = manhattan_dist(adv_pos, candidate_pos);
                    if (candidate_dist < min_dist) {
                        min_dist = candidate_dist;
                        min_pos = candidate_pos;
                    }   
                    break;
                case 3:
                    candidate_pos.x = curr_pos.x - 1;
                    candidate_pos.y = curr_pos.y;
                    candidate_dist = manhattan_dist(adv_pos, candidate_pos);
                    if (candidate_dist < min_dist) {
                        min_dist = candidate_dist;
                        min_pos = candidate_pos;
                    }   
                    break;
            }
        }
    }

    request.move_request = min_pos;

    /* fprintf(stderr, "Hunter(%d) was at (%d, %d), adv_pos was (%d,%d) and proposed (%d, %d), directions: %d%d%d%d\n", 
            getpid(), curr_pos.x, curr_pos.y, curr_state.adv_pos.x, curr_state.adv_pos.y, min_pos.x, min_pos.y, available_dirs[0], available_dirs[1],available_dirs[2],available_dirs[3]); */
    return request;
}

int main(int argc, char **argv) {
    int map_height, map_width;
    int directions[4] = {0};
    server_message last_state;
    ph_message request;
    pid_t pid = getpid();

    // Read map width and height
    map_width = atoi(argv[1]);
    map_height = atoi(argv[2]);



    while (1) {
        // Get state information
        read(0, &last_state, sizeof(server_message));

        // Generate request
        request = get_possible_move(last_state, map_width, map_height);

        // Send request
        write(1, &request, sizeof(ph_message));

        // Sleep for a random time
        usleep(10000*(1+rand()%9));
    }
    
    exit(0);
}
