#include "openconsole/package_measure.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    oc_package_measure measure;
    oc_package_measure_init(&measure, 8u, 3u);
    assert(oc_package_measure_visit(&measure, 0u, 0, 7u));
    assert(oc_package_measure_visit(&measure, 1u, 1, 999u));
    assert(oc_package_measure_visit(&measure, 2u, 0, 5u));
    assert(measure.total_bytes == 12u && measure.entries == 3u);
    assert(!oc_package_measure_visit(&measure, 0u, 0, 1u));
    assert(measure.result == OC_PACKAGE_MEASURE_ENTRIES);

    oc_package_measure_init(&measure, 2u, 8u);
    assert(!oc_package_measure_visit(&measure, 3u, 1, 0u));
    assert(measure.result == OC_PACKAGE_MEASURE_DEPTH);

    oc_package_measure_init(&measure, 8u, 8u);
    assert(oc_package_measure_visit(&measure, 0u, 0, UINT64_MAX));
    assert(!oc_package_measure_visit(&measure, 0u, 0, 1u));
    assert(measure.result == OC_PACKAGE_MEASURE_OVERFLOW);

    oc_package_measure_init(&measure, 8u, 8u);
    oc_package_measure_io_error(&measure);
    assert(measure.result == OC_PACKAGE_MEASURE_IO);
    assert(!oc_package_measure_visit(&measure, 0u, 0, 1u));
    puts("PASS: bounded package measurement and failure policy");
    return 0;
}
