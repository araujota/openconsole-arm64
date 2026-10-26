#ifndef OPENCONSOLE_TIME_FORMAT_H
#define OPENCONSOLE_TIME_FORMAT_H

#include <stdint.h>

#define OC_TIMEZONE_UNSPECIFIED 0x07ff

void oc_format_utc_time(char output[10], int available, uint8_t hour,
                        uint8_t minute, int16_t timezone, uint8_t daylight);

#endif
