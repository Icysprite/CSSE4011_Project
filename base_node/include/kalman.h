#ifndef KALMAN_H
#define KALMAN_H

#include <stdint.h>

struct kalman_filter {
    float x; 
    float p;  
    float q;  
    float r; 
};

void kalman_init(struct kalman_filter *kf, float q, float r, float initial_x);
float kalman_update(struct kalman_filter *kf, float measurement);

#endif /* KALMAN_H */