#include "openconsole/package_measure.h"

#include <stdint.h>

void oc_package_measure_init(oc_package_measure *measure,
                             uint32_t max_depth, uint32_t max_entries) {
    measure->total_bytes = 0u;
    measure->entries = 0u;
    measure->max_entries = max_entries;
    measure->max_depth = max_depth;
    measure->result = OC_PACKAGE_MEASURE_OK;
}

int oc_package_measure_visit(oc_package_measure *measure, uint32_t depth,
                             int directory, uint64_t file_size) {
    if (measure->result != OC_PACKAGE_MEASURE_OK) return 0;
    if (depth > measure->max_depth) {
        measure->result = OC_PACKAGE_MEASURE_DEPTH;
        return 0;
    }
    if (measure->entries >= measure->max_entries) {
        measure->result = OC_PACKAGE_MEASURE_ENTRIES;
        return 0;
    }
    ++measure->entries;
    if (!directory) {
        if (UINT64_MAX - measure->total_bytes < file_size) {
            measure->result = OC_PACKAGE_MEASURE_OVERFLOW;
            return 0;
        }
        measure->total_bytes += file_size;
    }
    return 1;
}

void oc_package_measure_io_error(oc_package_measure *measure) {
    if (measure->result == OC_PACKAGE_MEASURE_OK)
        measure->result = OC_PACKAGE_MEASURE_IO;
}
