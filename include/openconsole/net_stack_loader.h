#ifndef OPENCONSOLE_NET_STACK_LOADER_H
#define OPENCONSOLE_NET_STACK_LOADER_H

#include "openconsole/uefi.h"

#define OC_NET_STACK_MODULE_COUNT 12u

typedef int (*oc_net_stack_protocol_present)(void *, const EFI_GUID *);
typedef EFI_STATUS (*oc_net_stack_load_driver)(void *, const CHAR16 *,
                                               const EFI_GUID *);
typedef EFI_STATUS (*oc_net_stack_connect_loaded)(void *);

typedef struct {
    void *context;
    oc_net_stack_protocol_present protocol_present;
    oc_net_stack_load_driver load_driver;
    oc_net_stack_connect_loaded connect_loaded;
} oc_net_stack_loader_ops;

typedef struct {
    uint32_t already_present;
    uint32_t loaded;
    uint32_t failed_index;
} oc_net_stack_load_report;

EFI_STATUS oc_net_stack_load_missing(const oc_net_stack_loader_ops *,
                                     oc_net_stack_load_report *);

#endif
