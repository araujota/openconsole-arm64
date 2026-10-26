#ifndef OPENCONSOLE_PACKAGE_MEASURE_H
#define OPENCONSOLE_PACKAGE_MEASURE_H

#include <stdint.h>

typedef enum {
    OC_PACKAGE_MEASURE_OK = 0,
    OC_PACKAGE_MEASURE_DEPTH,
    OC_PACKAGE_MEASURE_ENTRIES,
    OC_PACKAGE_MEASURE_OVERFLOW,
    OC_PACKAGE_MEASURE_IO
} oc_package_measure_result;

typedef struct {
    uint64_t total_bytes;
    uint32_t entries;
    uint32_t max_entries;
    uint32_t max_depth;
    oc_package_measure_result result;
} oc_package_measure;

void oc_package_measure_init(oc_package_measure *measure,
                             uint32_t max_depth, uint32_t max_entries);
int oc_package_measure_visit(oc_package_measure *measure, uint32_t depth,
                             int directory, uint64_t file_size);
void oc_package_measure_io_error(oc_package_measure *measure);

#endif
