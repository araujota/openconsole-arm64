#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "openconsole/net_snp_bridge.h"

typedef struct {
    unsigned load, connect, disconnect, unload;
    EFI_HANDLE succeeds;
    unsigned fail_connect;
} fixture;

static EFI_STATUS load(void *context, EFI_HANDLE *image) {
    fixture *f = context; ++f->load; *image = (EFI_HANDLE)99; return EFI_SUCCESS;
}
static EFI_STATUS connect(void *context, EFI_HANDLE controller, EFI_HANDLE image) {
    fixture *f = context; (void)controller; assert(image == (EFI_HANDLE)99);
    ++f->connect; return f->fail_connect ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
static int probe(void *context, EFI_HANDLE controller) {
    return ((fixture *)context)->succeeds == controller;
}
static EFI_STATUS disconnect(void *context, EFI_HANDLE controller, EFI_HANDLE image) {
    fixture *f = context; (void)controller; (void)image; ++f->disconnect;
    return EFI_SUCCESS;
}
static EFI_STATUS unload(void *context, EFI_HANDLE image) {
    fixture *f = context; (void)image; ++f->unload; return EFI_SUCCESS;
}

int main(void) {
    EFI_HANDLE handles[2] = {(EFI_HANDLE)1, (EFI_HANDLE)2};
    oc_net_snp_bridge_report report;
    fixture f;
    oc_net_snp_bridge_ops ops = {&f, load, connect, probe, disconnect, unload};
    memset(&f, 0, sizeof(f));
    assert(oc_net_bridge_nii_to_snp(1, handles, 2, &ops, &report) == EFI_SUCCESS);
    assert(f.load == 0 && report.nii_handles == 2);
    memset(&f, 0, sizeof(f)); f.succeeds = handles[1];
    assert(oc_net_bridge_nii_to_snp(0, handles, 2, &ops, &report) == EFI_SUCCESS);
    assert(f.load == 1 && f.connect == 2 && f.disconnect == 1 &&
           report.selected_controller == handles[1]);
    memset(&f, 0, sizeof(f));
    assert(oc_net_bridge_nii_to_snp(0, handles, 2, &ops, &report) == EFI_NOT_FOUND);
    assert(f.connect == 2 && f.disconnect == 2 && f.unload == 1);
    memset(&f, 0, sizeof(f)); f.fail_connect = 1;
    assert(oc_net_bridge_nii_to_snp(0, handles, 2, &ops, &report) == EFI_NOT_FOUND);
    assert(report.connect_failures == 2 && f.disconnect == 0 && f.unload == 1);
    puts("PASS: bounded NII-to-SNP bridge selection and rollback");
    return 0;
}
