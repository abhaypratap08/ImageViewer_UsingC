#ifndef PHOTON_MOTION_H
#define PHOTON_MOTION_H

/* Values use the caller's units; velocities are units/second. */
typedef struct {
    float value, target, velocity;
} PhotonSpring;

void photon_spring_reset(PhotonSpring *spring, float value);
/* Retargeting preserves the current presentation value and velocity. */
void photon_spring_target(PhotonSpring *spring, float target);
/* dt and response are seconds; damping is a ratio (1 = critical).
 * Returns nonzero while moving. Reduced motion snaps to the exact target. */
int photon_spring_step(PhotonSpring *spring, float dt, float response,
                       float damping, int reduced_motion);

/* Velocity is units/second; deceleration_rate is the per-millisecond factor. */
float photon_project(float velocity, float deceleration_rate);
float photon_rubberband(float overshoot, float dimension);
/* Direct tracking inside [-limit, limit], rubber-banding outside. */
float photon_bound_drag(float position, float limit, float dimension);
float photon_clamp(float value, float minimum, float maximum);

typedef struct {
    float positions[8];
    double times[8];
    int count;
} PhotonVelocity;

/* Times are monotonic seconds. Samples/release queries use a 100 ms window. */
void photon_velocity_reset(PhotonVelocity *velocity, float position, double time);
void photon_velocity_add(PhotonVelocity *velocity, float position, double time);
float photon_velocity_get(const PhotonVelocity *velocity, double now);

#endif
