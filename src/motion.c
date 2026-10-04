#include "motion.h"

#include <float.h>
#include <math.h>

#define PHOTON_TAU 6.28318530717958647693
#define PHOTON_VELOCITY_WINDOW 0.100

static float finite_float(double value) {
    if (isnan(value)) return 0.0f;
    if (value > FLT_MAX) return FLT_MAX;
    if (value < -FLT_MAX) return -FLT_MAX;
    return (float)value;
}

float photon_clamp(float value, float minimum, float maximum) {
    value = finite_float(value);
    minimum = finite_float(minimum);
    maximum = finite_float(maximum);
    if (minimum > maximum) {
        float swap = minimum;
        minimum = maximum;
        maximum = swap;
    }
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

void photon_spring_reset(PhotonSpring *spring, float value) {
    if (!spring) return;
    spring->value = spring->target = isfinite(value) ? value : 0.0f;
    spring->velocity = 0.0f;
}

void photon_spring_target(PhotonSpring *spring, float target) {
    if (spring && isfinite(target)) spring->target = target;
}

int photon_spring_step(PhotonSpring *spring, float dt, float response,
                       float damping, int reduced_motion) {
    double omega, x, v, next_x, next_v, tolerance;
    if (!spring) return 0;
    if (!isfinite(spring->target))
        spring->target = isfinite(spring->value) ? spring->value : 0.0f;
    if (reduced_motion || !isfinite(spring->value) ||
        !isfinite(response) || response <= 0.0f) {
        photon_spring_reset(spring, spring->target);
        return 0;
    }
    if (!isfinite(spring->velocity)) spring->velocity = 0.0f;
    if (!isfinite(dt) || dt <= 0.0f)
        return spring->value != spring->target || spring->velocity != 0.0f;
    if (!isfinite(damping) || damping < 0.0f) damping = 1.0f;

    omega = PHOTON_TAU / response;
    x = (double)spring->value - spring->target;
    v = spring->velocity;

    /* Exact damped-oscillator solutions remain stable across long frames. */
    if (damping < 1.0f) {
        double decay = damping * omega;
        double frequency = omega * sqrt(1.0 - (double)damping * damping);
        double envelope = exp(-decay * dt);
        double cosine = cos(frequency * dt);
        double sine = sin(frequency * dt) / frequency;
        next_x = envelope * (x * cosine + (v + decay * x) * sine);
        next_v = envelope * (v * cosine - (decay * v + omega * omega * x) * sine);
    } else if (damping == 1.0f) {
        double envelope = exp(-omega * dt);
        double tangent = v + omega * x;
        next_x = (x + tangent * dt) * envelope;
        next_v = (v - omega * tangent * dt) * envelope;
    } else {
        /* Rationalized slow root avoids cancellation for large damping. */
        double factor = damping + sqrt((double)damping * damping - 1.0);
        double slow = -omega / factor;
        double fast = -omega * factor;
        double a = (v - fast * x) / (slow - fast) * exp(slow * dt);
        double b = (slow * x - v) / (slow - fast) * exp(fast * dt);
        next_x = a + b;
        next_v = slow * a + fast * b;
    }

    next_x += spring->target;
    if (!isfinite(next_x) || !isfinite(next_v) ||
        fabs(next_x) > FLT_MAX || fabs(next_v) > FLT_MAX) {
        photon_spring_reset(spring, spring->target);
        return 0;
    }
    spring->value = (float)next_x;
    spring->velocity = (float)next_v;
    /* Include float precision so large pixel coordinates cannot stall a ULP away. */
    tolerance = 0.0001 + 4.0 * FLT_EPSILON *
                fmax(fabs(spring->value), fabs(spring->target));
    if (fabs((double)spring->value - spring->target) <= tolerance &&
        fabs(spring->velocity) <= 0.001 + tolerance * omega) {
        photon_spring_reset(spring, spring->target);
        return 0;
    }
    return 1;
}

float photon_project(float velocity, float deceleration_rate) {
    if (!isfinite(velocity) || !isfinite(deceleration_rate) ||
        deceleration_rate < 0.0f || deceleration_rate >= 1.0f) return 0.0f;
    /* Apple's exponential-decay projection converts units/s to units/ms. */
    return finite_float((double)velocity / 1000.0 * deceleration_rate /
                        (1.0 - deceleration_rate));
}

float photon_rubberband(float overshoot, float dimension) {
    if (!isfinite(overshoot) || !isfinite(dimension) || dimension <= 0.0f)
        return 0.0f;
    return (float)((double)overshoot * dimension * 0.55 /
                   (dimension + 0.55 * fabs(overshoot)));
}

float photon_bound_drag(float position, float limit, float dimension) {
    if (!isfinite(position)) return 0.0f;
    if (!isfinite(limit) || limit < 0.0f) limit = 0.0f;
    if (position > limit)
        return finite_float((double)limit + photon_rubberband(position - limit, dimension));
    if (position < -limit)
        return finite_float(-(double)limit + photon_rubberband(position + limit, dimension));
    return position;
}

void photon_velocity_reset(PhotonVelocity *velocity, float position, double time) {
    if (!velocity) return;
    velocity->count = 0;
    if (!isfinite(position) || !isfinite(time)) return;
    velocity->positions[0] = position;
    velocity->times[0] = time;
    velocity->count = 1;
}

void photon_velocity_add(PhotonVelocity *velocity, float position, double time) {
    if (!velocity || !isfinite(position) || !isfinite(time)) return;
    if (velocity->count < 1 || velocity->count > 8 ||
        !isfinite(velocity->times[velocity->count - 1]) ||
        time < velocity->times[velocity->count - 1]) {
        photon_velocity_reset(velocity, position, time);
        return;
    }
    if (time == velocity->times[velocity->count - 1]) {
        /* Coalesced events update the sample rather than divide by zero later. */
        velocity->positions[velocity->count - 1] = position;
        return;
    }
    while (velocity->count > 0 &&
           (velocity->count == 8 || time - velocity->times[0] > PHOTON_VELOCITY_WINDOW)) {
        for (int i = 1; i < velocity->count; ++i) {
            velocity->positions[i - 1] = velocity->positions[i];
            velocity->times[i - 1] = velocity->times[i];
        }
        --velocity->count;
    }
    velocity->positions[velocity->count] = position;
    velocity->times[velocity->count] = time;
    ++velocity->count;
}

float photon_velocity_get(const PhotonVelocity *velocity, double now) {
    double sum_t = 0.0, sum_x = 0.0, sum_tt = 0.0, sum_tx = 0.0;
    double newest, origin, variance;
    int samples = 0;
    if (!velocity || velocity->count < 2 || velocity->count > 8 || !isfinite(now))
        return 0.0f;
    newest = velocity->times[velocity->count - 1];
    origin = velocity->positions[velocity->count - 1];
    if (!isfinite(newest) || !isfinite(origin) || now < newest ||
        now - newest >= PHOTON_VELOCITY_WINDOW) return 0.0f;

    /* Fit a slope over recent samples, relative to the newest to avoid losing
     * precision in large monotonic timestamps. A paused release has no fling. */
    for (int i = 0; i < velocity->count; ++i) {
        double t, x;
        if (!isfinite(velocity->times[i]) || !isfinite(velocity->positions[i]) ||
            (i > 0 && velocity->times[i] <= velocity->times[i - 1])) return 0.0f;
        if (now - velocity->times[i] > PHOTON_VELOCITY_WINDOW) continue;
        t = velocity->times[i] - newest;
        x = (double)velocity->positions[i] - origin;
        sum_t += t;
        sum_x += x;
        sum_tt += t * t;
        sum_tx += t * x;
        ++samples;
    }
    variance = samples * sum_tt - sum_t * sum_t;
    if (samples < 2 || variance <= 1e-12) return 0.0f;
    return finite_float((samples * sum_tx - sum_t * sum_x) / variance);
}
