#include "openconsole/time_format.h"

static void two_digits(char *output, unsigned value) {
    output[0] = (char)('0' + (value / 10u) % 10u);
    output[1] = (char)('0' + value % 10u);
}

void oc_format_utc_time(char output[10], int available, uint8_t hour,
                        uint8_t minute, int16_t timezone, uint8_t daylight) {
    int total;
    (void)daylight; /* EFI_TIME already reports the adjusted wall-clock hour. */
    if (!available || hour > 23u || minute > 59u) {
        output[0] = '-'; output[1] = '-'; output[2] = ':';
        output[3] = '-'; output[4] = '-';
    } else {
        total = (int)hour * 60 + (int)minute;
        if (timezone != OC_TIMEZONE_UNSPECIFIED &&
            timezone >= -1440 && timezone <= 1440)
            total -= timezone;
        total %= 1440;
        if (total < 0) total += 1440;
        two_digits(output, (unsigned)total / 60u);
        output[2] = ':';
        two_digits(output + 3, (unsigned)total % 60u);
    }
    output[5] = ' '; output[6] = 'U'; output[7] = 'T'; output[8] = 'C';
    output[9] = '\0';
}
