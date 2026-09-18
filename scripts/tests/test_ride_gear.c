#include <assert.h>
#include <stdio.h>
#include "../../main/ride_gear_state.h"

int main(void)
{
    unsigned checks = 0;
    assert(ride_gear_symbol(NULL) == '-'); ++checks;
    for (unsigned valid = 0; valid < 2; ++valid) {
        for (unsigned state = 0; state < 8; ++state) {
            for (unsigned profile = 0; profile < 5; ++profile) {
                vesc_ride_safety_t s = {
                    .valid = valid, .state = state, .current_profile = profile
                };
                char expected = '-';
                if (valid && state == VESC_RIDE_SAFETY_PARK) expected = 'P';
                if (valid && state == VESC_RIDE_SAFETY_FORWARD && profile < 3)
                    expected = '1' + profile;
                if (valid && (state == VESC_RIDE_SAFETY_REVERSE_READY ||
                              state == VESC_RIDE_SAFETY_REVERSE_ACTIVE)) expected = 'R';
                assert(ride_gear_symbol(&s) == expected); ++checks;
            }
        }
    }
    printf("Ride gear: %u checks PASS\n", checks);
    return 0;
}
