#include "openconsole/time_format.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect(const char *wanted, int available, unsigned hour,
                   unsigned minute, int timezone, unsigned daylight) {
    char value[10];
    oc_format_utc_time(value, available, (uint8_t)hour, (uint8_t)minute,
                       (int16_t)timezone, (uint8_t)daylight);
    assert(strcmp(value, wanted) == 0);
}

int main(void) {
    expect("20:42 UTC", 1, 20, 42, OC_TIMEZONE_UNSPECIFIED, 0);
    expect("18:00 UTC", 1, 10, 0, -480, 0);
    expect("01:30 UTC", 1, 3, 30, 120, 0);
    expect("23:30 UTC", 1, 0, 30, 60, 0);
    expect("00:30 UTC", 1, 23, 30, -60, 0);
    expect("12:34 UTC", 1, 12, 34, 1441, 0);
    expect("12:34 UTC", 1, 12, 34, OC_TIMEZONE_UNSPECIFIED, 2);
    expect("--:-- UTC", 0, 0, 0, 0, 0);
    expect("--:-- UTC", 1, 24, 0, 0, 0);
    expect("--:-- UTC", 1, 23, 60, 0, 0);
    puts("PASS: deterministic UTC firmware-time formatting");
    return 0;
}
