#define HAVE_STRUCT_TIMESPEC
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <io.h>
#include <ddkernel.h>
//#include <unistd.h>

sem_t global_sem, hol_block[4], turn_left_block[4], straight_block[4], turn_right_block[4];
sem_t hol_south, hol_west, hol_north, hol_east;

pthread_mutex_t hol_south_mutex, hol_west_mutex, hol_north_mutex, hol_east_mutex;
pthread_mutex_t turn_left_mutex[4], straight_mutex[4], turn_right_mutex[4];

int green_dur_in_sec = 18;
int yellow_dur_in_sec = 2;
int red_dur_in_sec = 20;
int turn_left_dur_in_sec = 3;
int straight_dur_in_sec = 2;
int turn_right_dur_in_sec = 1;

int sim_dur = 30;
int time_counter = 0;
int traffic_state = 0;
int ns_state = 0;
int we_state = 2;

typedef struct _directions {
    char dir_original;
    char dir_target;
} directions;

typedef struct _cars {
    int cid;
    int arrival_time;
    directions direction;
} cars;

int car_count = 8;
//cars car_zero = { .cid = 0, .arrival_time = 10, .direction.dir_original = '^', .direction.dir_target = '^' };
cars all_cars[8] = {
    {.cid = 0, .arrival_time = 10, .direction.dir_original = '^', .direction.dir_target = '^' },
    {.cid = 1, .arrival_time = 19, .direction.dir_original = '^', .direction.dir_target = '^' },
    {.cid = 2, .arrival_time = 32, .direction.dir_original = '^', .direction.dir_target = '<' },
    {.cid = 3, .arrival_time = 34, .direction.dir_original = 'v', .direction.dir_target = 'v' },
    {.cid = 4, .arrival_time = 41, .direction.dir_original = 'v', .direction.dir_target = '>' },
    {.cid = 5, .arrival_time = 43, .direction.dir_original = '^', .direction.dir_target = '^' },
    {.cid = 6, .arrival_time = 56, .direction.dir_original = '>', .direction.dir_target = '^' },
    {.cid = 7, .arrival_time = 58, .direction.dir_original = '<', .direction.dir_target = '^' }
};

char GetLightState(int stateNum) {
    if (stateNum == 0) return 'G';
    else if (stateNum == 1) return 'Y';
    else if (stateNum == 2) return 'R';
}

int GetLight(char direction) {
    if (direction == '^' || direction == 'v') return ns_state;
    else if (direction == '>' || direction == '<') return we_state;
}

int GetIntersectionNum(char direction) {
    if (direction == '^') return 0;
    else if (direction == '>') return 1;
    else if (direction == 'v') return 2;
    else if (direction == '<') return 3;
}

char GetIntersectionSymbol(int num) {
    if (num == 0) return '^';
    else if (num == 1) return '>';
    else if (num == 2) return 'v';
    else if (num == 3) return '<';
}

int GetCrossingDuration(directions dir) {
    // straight through
    if (dir.dir_original == dir.dir_target) return straight_dur_in_sec;

    // turning right
    else if (GetIntersectionNum(dir.dir_original) == GetIntersectionNum(dir.dir_target) - 1) return turn_right_dur_in_sec;

    // turning left
    else if (GetIntersectionNum(dir.dir_original) == (GetIntersectionNum(dir.dir_target) + 1) % 4) return turn_left_dur_in_sec;
}

void BlockCrossingSemaphore(directions dir) {
    // straight through
    if (dir.dir_original == dir.dir_target) {
        sem_wait(&straight_block[GetIntersectionNum(dir.dir_original)]);
        sem_wait(&turn_left_block[(GetIntersectionNum(dir.dir_original) + 2) % 4]); // left turn from opposite direction
        sem_wait(&turn_right_block[(GetIntersectionNum(dir.dir_original) + 3) % 4]); // right turn from the right lane
    }
    // turning right
    else if (GetIntersectionNum(dir.dir_original) == GetIntersectionNum(dir.dir_target) - 1) {
        sem_wait(&turn_right_block[GetIntersectionNum(dir.dir_original)]);
        sem_wait(&straight_block[(GetIntersectionNum(dir.dir_original) + 1) % 4]); // straight from the left lane
        sem_wait(&turn_left_block[(GetIntersectionNum(dir.dir_original) + 2) % 4]); // left turn from opposite direction
    }
    // turning left
    else if (GetIntersectionNum(dir.dir_original) == (GetIntersectionNum(dir.dir_target) + 1) % 4) {
        sem_wait(&turn_left_block[GetIntersectionNum(dir.dir_original)]);
        sem_wait(&straight_block[(GetIntersectionNum(dir.dir_original) + 2) % 4]); // straight from opposite direction
        sem_wait(&turn_right_block[(GetIntersectionNum(dir.dir_original) + 2) % 4]); // right turn from opposite direction
    }
}

void UnblockCrossingSemaphore(directions dir) {
    // straight through
    if (dir.dir_original == dir.dir_target) {
        sem_post(&straight_block[GetIntersectionNum(dir.dir_original)]);
        sem_post(&turn_left_block[(GetIntersectionNum(dir.dir_original) + 2) % 4]); // left turn from opposite direction
        sem_post(&turn_right_block[(GetIntersectionNum(dir.dir_original) + 3) % 4]); // right turn from the right lane
    }
    // turning right
    else if (GetIntersectionNum(dir.dir_original) == GetIntersectionNum(dir.dir_target) - 1) {
        sem_post(&turn_right_block[GetIntersectionNum(dir.dir_original)]);
        sem_post(&straight_block[(GetIntersectionNum(dir.dir_original) + 1) % 4]); // straight from the left lane
        sem_post(&turn_left_block[(GetIntersectionNum(dir.dir_original) + 2) % 4]); // left turn from opposite direction
    }
    // turning left
    else if (GetIntersectionNum(dir.dir_original) == (GetIntersectionNum(dir.dir_target) + 1) % 4) {
        sem_post(&turn_left_block[GetIntersectionNum(dir.dir_original)]);
        sem_post(&straight_block[(GetIntersectionNum(dir.dir_original) + 2) % 4]); // straight from opposite direction
        sem_post(&turn_right_block[(GetIntersectionNum(dir.dir_original) + 2) % 4]); // right turn from opposite direction
    }
}

void ArriveIntersection(int id, directions dir) {
    boolean crossing = FALSE;
    while (!crossing) {
        sem_wait(&hol_block[GetIntersectionNum(dir.dir_original)]);
        printf("Time %.1f: Car %d (%c %c) arriving\n", time_counter / 10.0, id, dir.dir_original, dir.dir_target);

        // wait until no one is blocking
        BlockCrossingSemaphore(dir);
        // when its okay to cross
        crossing = TRUE;
    }
}

void CrossIntersection(int id, directions dir) {
    // unlock head-of-line block when start crossing
    sem_post(&hol_block[GetIntersectionNum(dir.dir_original)]);
    printf("Time %.1f: Car %d (%c %c)          crossing\n", time_counter / 10.0, id, dir.dir_original, dir.dir_target);
    boolean crossing = TRUE;
    // lock this specific crossing semaphore and sleep for the duration
    while (crossing) {
        if (time_counter >= all_cars[id].arrival_time + GetCrossingDuration(dir) * 10) {
            crossing = FALSE;
        }
    }
}

void ExitIntersection(int id, directions dir) {
    printf("Time %.1f: Car %d (%c %c)                   exiting\n", time_counter / 10.0, id, dir.dir_original, dir.dir_target);
    UnblockCrossingSemaphore(dir);
}

void Car(int id, directions dir) {
    ArriveIntersection(id, dir);
    CrossIntersection(id, dir);
    ExitIntersection(id, dir);
}

void TrafficControl() {
    while (time_counter < (sim_dur * 10)) {
        sem_wait(&global_sem);
        if (time_counter == 0) {
            //printf("Time  %.1f\n", time_counter / 10.0);
            //printf("North-South: %c\nWest-East: %c\n", GetLightState(ns_state), GetLightState(we_state));
        }

        Sleep(100);
        time_counter++;
        //printf("Time  %.1f\n", time_counter / 10.0);
        // Manipulate lights state
        if (time_counter % (red_dur_in_sec * 10) == (green_dur_in_sec * 10)) {
            traffic_state = 1;
            if (ns_state == 0) ns_state++;
            if (we_state == 0) we_state++;
            //printf("North-South: %c\nWest-East: %c\n", GetLightState(ns_state), GetLightState(we_state));
        }
        else if (time_counter % (red_dur_in_sec * 10) == 0 && time_counter != 0) {
            traffic_state = 0;
            if (ns_state == 1) ns_state++;
            else if (ns_state == 2) ns_state = 0;
            if (we_state == 1) we_state++;
            else if (we_state == 2) we_state = 0;
            //printf("North-South: %c\nWest-East: %c\n", GetLightState(ns_state), GetLightState(we_state));
        }
        sem_post(&global_sem);
    }
}

//void CarControl(void* index) {
//    boolean not_arrived = TRUE;
//    while (not_arrived) {
//        //sem_wait(&global_sem);
//        int idx = (int)index;
//        
//        if (time_counter == all_cars[idx].arrival_time) {
//            //printf("Car %d arrived!\n", idx);
//            Car(all_cars[idx].cid, all_cars[idx].direction);
//            not_arrived = FALSE;
//        }
//    }
//}

//void CarControl(void* index) {
//    boolean not_arrived = TRUE;
//    cars car = all_cars[(int)index];
//    int semval;
//    while (not_arrived) {
//        //sem_wait(&global_sem);
//
//        if (time_counter == car.arrival_time) {
//            //printf("Car %d arrived!\n", idx);
//            //Car(all_cars[idx].cid, all_cars[idx].direction);
//            not_arrived = FALSE;
//
//            // ArriveIntersection
//            sem_wait(&hol_block[GetIntersectionNum(car.direction.dir_original)]);
//            
//            if (car.direction.dir_original == '^') {
//                sem_wait(&hol_south);
//                sem_getvalue(&hol_south, &semval);
//                printf("South Head-of-line Semaphore: %d\n", semval);
//            }
//            if (car.direction.dir_original == '>') {
//                sem_wait(&hol_west);
//                sem_getvalue(&hol_west, &semval);
//                printf("West Head-of-line Semaphore: %d\n", semval);
//            }
//            if (car.direction.dir_original == 'v') {
//                sem_wait(&hol_north);
//                sem_getvalue(&hol_north, &semval);
//                printf("North Head-of-line Semaphore: %d\n", semval);
//            }
//            if (car.direction.dir_original == '<') {
//                sem_wait(&hol_east);
//                sem_getvalue(&hol_east, &semval);
//                printf("East Head - of - line Semaphore : % d\n", semval);
//            }
//
//            printf("Time %.1f: Car %d (%c %c) arriving\n", time_counter / 10.0, car.cid, car.direction.dir_original, car.direction.dir_target);
//            sem_getvalue(&hol_block[GetIntersectionNum(car.direction.dir_original)], &semval);
//            printf("Head-of-line Block at %c Semaphore: %d\n", car.direction.dir_original, semval);
//            
//            printf("Checking all crossing blocks:\n");
//            for (int i = 0; i < 4; i++) {
//                sem_getvalue(&straight_block[i], &semval);
//                printf("Straight from %c Block Semaphore: %d\n", GetIntersectionSymbol(i), semval);
//                sem_getvalue(&turn_left_block[i], &semval);
//                printf("Left from %c Block Semaphore: %d\n", GetIntersectionSymbol(i), semval);
//                sem_getvalue(&turn_right_block[i], &semval);
//                printf("Right from %c Block Semaphore: %d\n", GetIntersectionSymbol(i), semval);
//            }
//
//            // checks for clearance + blocks for crossing
//            printf("Blocking for crossing...\n");
//            // straight through
//            if (car.direction.dir_original == car.direction.dir_target) {
//                sem_wait(&straight_block[GetIntersectionNum(car.direction.dir_original)]);
//                sem_wait(&turn_left_block[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // left turn from opposite direction
//                sem_wait(&turn_right_block[(GetIntersectionNum(car.direction.dir_original) + 3) % 4]); // right turn from the right lane
//            }
//            // turning right
//            else if (GetIntersectionNum(car.direction.dir_original) == GetIntersectionNum(car.direction.dir_target) - 1) {
//                sem_wait(&turn_right_block[GetIntersectionNum(car.direction.dir_original)]);
//                sem_wait(&straight_block[(GetIntersectionNum(car.direction.dir_original) + 1) % 4]); // straight from the left lane
//                sem_wait(&turn_left_block[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // left turn from opposite direction
//            }
//            // turning left
//            else if (GetIntersectionNum(car.direction.dir_original) == (GetIntersectionNum(car.direction.dir_target) + 1) % 4) {
//                sem_wait(&turn_left_block[GetIntersectionNum(car.direction.dir_original)]);
//                sem_wait(&straight_block[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // straight from opposite direction
//                sem_wait(&turn_right_block[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // right turn from opposite direction
//            }
//
//            printf("Checking all crossing blocks:\n");
//            for (int i = 0; i < 4; i++) {
//                sem_getvalue(&straight_block[i], &semval);
//                printf("Straight from %c Block Semaphore: %d\n", GetIntersectionSymbol(i), semval);
//                sem_getvalue(&turn_left_block[i], &semval);
//                printf("Left from %c Block Semaphore: %d\n", GetIntersectionSymbol(i), semval);
//                sem_getvalue(&turn_right_block[i], &semval);
//                printf("Right from %c Block Semaphore: %d\n", GetIntersectionSymbol(i), semval);
//            }
//
//            // CrossIntersection
//            // unlock head-of-line block
//            sem_post(&hol_block[GetIntersectionNum(car.direction.dir_original)]);
//
//            if (car.direction.dir_original == '^') {
//                sem_post(&hol_south);
//                sem_getvalue(&hol_south, &semval);
//                printf("South Head-of-line Semaphore: %d\n", semval);
//            }
//            if (car.direction.dir_original == '>') {
//                sem_post(&hol_west);
//                sem_getvalue(&hol_west, &semval);
//                printf("West Head-of-line Semaphore: %d\n", semval);
//            }
//            if (car.direction.dir_original == 'v') {
//                sem_post(&hol_north);
//                sem_getvalue(&hol_north, &semval);
//                printf("North Head-of-line Semaphore: %d\n", semval);
//            }
//            if (car.direction.dir_original == '<') {
//                sem_post(&hol_east);
//                sem_getvalue(&hol_east, &semval);
//                printf("East Head - of - line Semaphore : % d\n", semval);
//            }
//
//            printf("Time %.1f: Car %d (%c %c)          crossing\n", time_counter / 10.0, car.cid, car.direction.dir_original, car.direction.dir_target);
//            sem_getvalue(&hol_block[GetIntersectionNum(car.direction.dir_original)], &semval);
//            printf("Head-of-line Block at %c Semaphore: %d\n", car.direction.dir_original, semval);
//            boolean crossing = TRUE;
//            while (crossing) {
//                if (time_counter >= car.arrival_time + GetCrossingDuration(car.direction) * 10) {
//                    crossing = FALSE;
//                }
//            }
//
//            // ExitIntersection
//            printf("Time %.1f: Car %d (%c %c)                   exiting\n", time_counter / 10.0, car.cid, car.direction.dir_original, car.direction.dir_target);
//            // unblocks for crossing
//            // straight through
//            if (car.direction.dir_original == car.direction.dir_target) {
//                sem_post(&straight_block[GetIntersectionNum(car.direction.dir_original)]);
//                sem_post(&turn_left_block[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // left turn from opposite direction
//                sem_post(&turn_right_block[(GetIntersectionNum(car.direction.dir_original) + 3) % 4]); // right turn from the right lane
//            }
//            // turning right
//            else if (GetIntersectionNum(car.direction.dir_original) == GetIntersectionNum(car.direction.dir_target) - 1) {
//                sem_post(&turn_right_block[GetIntersectionNum(car.direction.dir_original)]);
//                sem_post(&straight_block[(GetIntersectionNum(car.direction.dir_original) + 1) % 4]); // straight from the left lane
//                sem_post(&turn_left_block[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // left turn from opposite direction
//            }
//            // turning left
//            else if (GetIntersectionNum(car.direction.dir_original) == (GetIntersectionNum(car.direction.dir_target) + 1) % 4) {
//                sem_post(&turn_left_block[GetIntersectionNum(car.direction.dir_original)]);
//                sem_post(&straight_block[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // straight from opposite direction
//                sem_post(&turn_right_block[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // right turn from opposite direction
//            }
//
//            printf("Checking all crossing blocks:\n");
//            for (int i = 0; i < 4; i++) {
//                sem_getvalue(&straight_block[i], &semval);
//                printf("Straight from %c Block Semaphore: %d\n", GetIntersectionSymbol(i), semval);
//                sem_getvalue(&turn_left_block[i], &semval);
//                printf("Left from %c Block Semaphore: %d\n", GetIntersectionSymbol(i), semval);
//                sem_getvalue(&turn_right_block[i], &semval);
//                printf("Right from %c Block Semaphore: %d\n", GetIntersectionSymbol(i), semval);
//            }
//        }
//    }
//}

void CarControl(void* index) {
    boolean not_arrived = TRUE;
    cars car = all_cars[(int)index];
    directions previous_car_dir;
    if ((int)index == 0) previous_car_dir = car.direction;
    else previous_car_dir = all_cars[(int)index - 1].direction;
    
    while (not_arrived) {
        //sem_wait(&global_sem);
    
        if (time_counter == car.arrival_time) {
            not_arrived = FALSE;
    
            // ArriveIntersection
            boolean crossing = FALSE;
            printf("Time %.1f: Car %d (%c %c) arriving\n", time_counter / 10.0, car.cid, car.direction.dir_original, car.direction.dir_target);
            if (car.direction.dir_original == '^') pthread_mutex_lock(&hol_south_mutex);
            if (car.direction.dir_original == '>') pthread_mutex_lock(&hol_west_mutex);
            if (car.direction.dir_original == 'v') pthread_mutex_lock(&hol_north_mutex);
            if (car.direction.dir_original == '<') pthread_mutex_lock(&hol_east_mutex);

            // checks traffic light
            int light_state;
            while (!crossing) {
                light_state = GetLight(car.direction.dir_original);

                // green light
                if (light_state == 0) {
                    // straight through
                    if (car.direction.dir_original == car.direction.dir_target) {
                        pthread_mutex_lock(&straight_mutex[GetIntersectionNum(car.direction.dir_original)]);
                        pthread_mutex_trylock(&turn_left_mutex[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // left turn from opposite direction
                        pthread_mutex_trylock(&turn_right_mutex[(GetIntersectionNum(car.direction.dir_original) + 3) % 4]); // right turn from the right lane
                    }
                    // turning right
                    else if ((GetIntersectionNum(car.direction.dir_original) + 1) % 4 == GetIntersectionNum(car.direction.dir_target)) {
                        pthread_mutex_lock(&turn_right_mutex[GetIntersectionNum(car.direction.dir_original)]);
                        pthread_mutex_trylock(&straight_mutex[(GetIntersectionNum(car.direction.dir_original) + 1) % 4]); // straight from the left lane
                        pthread_mutex_trylock(&turn_left_mutex[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // left turn from opposite direction
                    }
                    // turning left
                    else if (GetIntersectionNum(car.direction.dir_original) == (GetIntersectionNum(car.direction.dir_target) + 1) % 4) {
                        pthread_mutex_lock(&turn_left_mutex[GetIntersectionNum(car.direction.dir_original)]);
                        pthread_mutex_trylock(&straight_mutex[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // straight from opposite direction
                        pthread_mutex_trylock(&turn_right_mutex[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // right turn from opposite direction
                    }
                    crossing = TRUE;
                }
                else if (light_state == 1) {
                    pthread_mutex_lock(&turn_right_mutex[GetIntersectionNum(car.direction.dir_original)]);
                    pthread_mutex_trylock(&straight_mutex[(GetIntersectionNum(car.direction.dir_original) + 1) % 4]); // straight from the left lane
                    pthread_mutex_trylock(&turn_left_mutex[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // left turn from opposite direction
                    crossing = TRUE;
                }
                else if (light_state == 2) {
                    // turning right
                    if ((GetIntersectionNum(car.direction.dir_original) + 1) % 4 == GetIntersectionNum(car.direction.dir_target)) {
                        pthread_mutex_lock(&turn_right_mutex[GetIntersectionNum(car.direction.dir_original)]);
                        pthread_mutex_trylock(&straight_mutex[(GetIntersectionNum(car.direction.dir_original) + 1) % 4]); // straight from the left lane
                        pthread_mutex_trylock(&turn_left_mutex[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // left turn from opposite direction
                        crossing = TRUE;
                    }
                    // else wait
                }
            }
    
            // CrossIntersection
            // unlock head-of-line block
            printf("Time %.1f: Car %d (%c %c)          crossing\n", time_counter / 10.0, car.cid, car.direction.dir_original, car.direction.dir_target);
            int crossing_time = time_counter;
            if (car.direction.dir_original == '^') pthread_mutex_unlock(&hol_south_mutex);
            if (car.direction.dir_original == '>') pthread_mutex_unlock(&hol_west_mutex);
            if (car.direction.dir_original == 'v') pthread_mutex_unlock(&hol_north_mutex);
            if (car.direction.dir_original == '<') pthread_mutex_unlock(&hol_east_mutex);

            // unlock own's crossing block
            if (car.direction.dir_original == car.direction.dir_target)
                pthread_mutex_unlock(&straight_mutex[GetIntersectionNum(car.direction.dir_original)]); // straight through
            else if (GetIntersectionNum(car.direction.dir_original) == GetIntersectionNum(car.direction.dir_target) - 1) 
                pthread_mutex_unlock(&turn_right_mutex[GetIntersectionNum(car.direction.dir_original)]); // turning right
            else if (GetIntersectionNum(car.direction.dir_original) == (GetIntersectionNum(car.direction.dir_target) + 1) % 4) 
                pthread_mutex_unlock(&turn_left_mutex[GetIntersectionNum(car.direction.dir_original)]); // turning left
            
            while (crossing) {
                if (time_counter >= crossing_time + GetCrossingDuration(car.direction) * 10) {
                    crossing = FALSE;
                }
            }
    
            // ExitIntersection
            printf("Time %.1f: Car %d (%c %c)                   exiting\n", time_counter / 10.0, car.cid, car.direction.dir_original, car.direction.dir_target);
            // unblocks for crossing
            // straight through
            if (car.direction.dir_original == car.direction.dir_target) {
                pthread_mutex_unlock(&straight_mutex[GetIntersectionNum(car.direction.dir_original)]);
                pthread_mutex_unlock(&turn_left_mutex[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // left turn from opposite direction
                pthread_mutex_unlock(&turn_right_mutex[(GetIntersectionNum(car.direction.dir_original) + 3) % 4]); // right turn from the right lane
            }
            // turning right
            else if ((GetIntersectionNum(car.direction.dir_original) + 1) % 4 == GetIntersectionNum(car.direction.dir_target)) {
                pthread_mutex_unlock(&turn_right_mutex[GetIntersectionNum(car.direction.dir_original)]);
                pthread_mutex_unlock(&straight_mutex[(GetIntersectionNum(car.direction.dir_original) + 1) % 4]); // straight from the left lane
                pthread_mutex_unlock(&turn_left_mutex[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // left turn from opposite direction
            }
            // turning left
            else if (GetIntersectionNum(car.direction.dir_original) == (GetIntersectionNum(car.direction.dir_target) + 1) % 4) {
                pthread_mutex_unlock(&turn_left_mutex[GetIntersectionNum(car.direction.dir_original)]);
                pthread_mutex_unlock(&straight_mutex[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // straight from opposite direction
                pthread_mutex_unlock(&turn_right_mutex[(GetIntersectionNum(car.direction.dir_original) + 2) % 4]); // right turn from opposite direction
            }
        }
    }
}

int main(void) {
    sem_init(&global_sem, 1, 1);
    /*sem_init(&hol_north, 1, 1);
    sem_init(&hol_south, 1, 1);
    sem_init(&hol_west, 1, 1);
    sem_init(&hol_east, 1, 1);*/
    /*for (int intersection = 0; intersection < 4; intersection++) {
        sem_init(&hol_block[intersection], 1, 1);
        sem_init(&turn_left_block[intersection], 1, 1);
        sem_init(&straight_block[intersection], 1, 1);
        sem_init(&turn_right_block[intersection], 1, 1);
    }*/

    pthread_mutex_init(&hol_north_mutex, NULL);
    pthread_mutex_init(&hol_south_mutex, NULL);
    pthread_mutex_init(&hol_west_mutex, NULL);
    pthread_mutex_init(&hol_east_mutex, NULL);

    for (int i = 0; i < 4; i++) {
        pthread_mutex_init(&turn_left_mutex[i], NULL);
        pthread_mutex_init(&straight_mutex[i], NULL);
        pthread_mutex_init(&turn_right_mutex[i], NULL);
    }

    pthread_t* thread_traffic_light;
    pthread_t* thread_cars;

    thread_traffic_light = (pthread_t*)malloc(sizeof(*thread_traffic_light));
    thread_cars = (pthread_t*)malloc(car_count * sizeof(pthread_t));

    // start the thread
    //printf("Starting thread, semaphore is unlocked.\n");
    pthread_create(thread_traffic_light, NULL, (void*)TrafficControl, NULL);
    for (int index = 0; index < car_count; index++) {
        pthread_create(&thread_cars[index], NULL, (void*)CarControl, (void*)index);
    }

    //getchar();

    //sem_wait(&global_sem);
    //printf("Semaphore locked.\n");

    //// do stuff with whatever is shared between threads
    //getchar();

    //printf("Semaphore unlocked.\n");
    //sem_post(&global_sem);

    if (pthread_join(*thread_traffic_light, NULL) != 0) {
        return 1;
    }
    for (int index = 0; index < car_count; index++) {
        if (pthread_join(thread_cars[index], NULL) != 0) {
            return 2 + index;
        }
    }

    //getchar();

    //sem_wait(&global_sem);
    //printf("Main thread terminated.\n");
    free(thread_traffic_light);
    free(thread_cars);

    pthread_mutex_destroy(&hol_north_mutex);
    pthread_mutex_destroy(&hol_south_mutex);
    pthread_mutex_destroy(&hol_west_mutex);
    pthread_mutex_destroy(&hol_east_mutex);
    for (int i = 0; i < 4; i++) {
        pthread_mutex_destroy(&turn_left_mutex[i]);
        pthread_mutex_destroy(&straight_mutex[i]);
        pthread_mutex_destroy(&turn_right_mutex[i]);
    }

    /*sem_destroy(&global_sem);
    sem_destroy(&hol_north);
    sem_destroy(&hol_south);
    sem_destroy(&hol_west);
    sem_destroy(&hol_east);
    for (int intersection = 0; intersection < 4; intersection++) {
        sem_destroy(&hol_block[intersection]);
        sem_destroy(&turn_left_block[intersection]);
        sem_destroy(&straight_block[intersection]);
        sem_destroy(&turn_right_block[intersection]);
    }*/
    return 0;
}