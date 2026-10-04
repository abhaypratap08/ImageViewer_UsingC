#include "../src/motion.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void test_spring_retargeting(void) {
    PhotonSpring spring;
    float velocity;

    photon_spring_reset(&spring, 0.0f);
    photon_spring_target(&spring, 100.0f);
    assert(photon_spring_step(&spring, 1.0f / 60.0f, 0.4f, 1.0f, 0));
    velocity = spring.velocity;
    photon_spring_target(&spring, 20.0f);
    assert(spring.value > 0.0f && spring.value < 100.0f);
    assert(fabsf(spring.velocity - velocity) < 0.001f);

    for (int i = 0; i < 240 && photon_spring_step(&spring, 1.0f / 60.0f,
                                                   0.4f, 1.0f, 0); ++i) {}
    assert(spring.value == 20.0f);
    assert(spring.velocity == 0.0f);

    photon_spring_target(&spring, 80.0f);
    assert(photon_spring_step(&spring, 1.0f / 60.0f, 0.4f, 1.0f, 1) == 0);
    assert(spring.value == 80.0f);
}

static void test_projection_and_bounds(void) {
    PhotonVelocity velocity = {0};

    assert(fabsf(photon_project(1000.0f, 0.998f) - 499.0f) < 0.01f);
    assert(photon_rubberband(100.0f, 500.0f) > 0.0f);
    assert(photon_rubberband(-100.0f, 500.0f) < 0.0f);
    assert(photon_bound_drag(700.0f, 500.0f, 1000.0f) > 500.0f);
    assert(photon_bound_drag(-700.0f, 500.0f, 1000.0f) < -500.0f);

    photon_velocity_reset(&velocity, 0.0f, 1.0);
    photon_velocity_add(&velocity, 10.0f, 1.02);
    photon_velocity_add(&velocity, 30.0f, 1.04);
    assert(fabsf(photon_velocity_get(&velocity, 1.04) - 750.0f) < 0.01f);
    assert(photon_velocity_get(&velocity, 1.20) == 0.0f);
}

static void test_boundaries_and_frame_rates(void) {
    PhotonSpring slow, fast;
    photon_spring_reset(&slow, -80);
    photon_spring_reset(&fast, -80);
    slow.velocity = fast.velocity = 500;
    photon_spring_target(&slow, 50);
    photon_spring_target(&fast, 50);
    for (int i = 0; i < 30; i++) photon_spring_step(&slow, 1.0f / 30, 0.4f, 0.8f, 0);
    for (int i = 0; i < 144; i++) photon_spring_step(&fast, 1.0f / 144, 0.4f, 0.8f, 0);
    assert(fabsf(slow.value - fast.value) < 0.001f);
    assert(fabsf(slow.velocity - fast.velocity) < 0.01f);

    photon_spring_reset(&slow, 0);
    photon_spring_target(&slow, 100);
    float previous = slow.value;
    for (int i = 0; i < 240; i++) {
        photon_spring_step(&slow, 1.0f / 120, 0.4f, 1, 0);
        assert(slow.value >= previous && slow.value <= 100);
        previous = slow.value;
    }
    photon_spring_target(&slow, -50);
    photon_spring_step(&slow, 0, 0.4f, 1, 0);
    assert(slow.value == previous);
    photon_spring_step(&slow, 10, 0.4f, 1, 0);
    assert(slow.value == -50 && slow.velocity == 0);
    photon_spring_target(&slow, NAN);
    assert(slow.target == -50);
    assert(photon_project(1000, 1) == 0 && photon_project(INFINITY, 0.998f) == 0);
    assert(photon_bound_drag(25, 100, 500) == 25);
    assert(photon_rubberband(1000, 500) > photon_rubberband(100, 500));
    assert(photon_rubberband(1000, 500) < 500);

    PhotonVelocity history = {0};
    photon_velocity_reset(&history, 0, 1);
    photon_velocity_add(&history, 10, 1);
    assert(history.count == 1 && photon_velocity_get(&history, 1) == 0);
    photon_velocity_add(&history, 20, 1.02);
    assert(fabsf(photon_velocity_get(&history, 1.02) - 500) < 0.01f);
    photon_velocity_add(&history, 0, 0.5);
    assert(history.count == 1 && photon_velocity_get(&history, 0.5) == 0);
}

int main(void) {
    test_spring_retargeting();
    test_projection_and_bounds();
    test_boundaries_and_frame_rates();
    puts("motion tests passed");
    return 0;
}
