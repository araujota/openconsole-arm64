#ifndef OPENCONSOLE_EXAMPLE_PACKAGE_ROOT_H
#define OPENCONSOLE_EXAMPLE_PACKAGE_ROOT_H

#include "openconsole/uefi.h"

/* Opens the directory containing the running EFI image on the same volume.
 * The caller owns the returned EFI_FILE_PROTOCOL and must close it. */
EFI_STATUS oc_example_open_package_root(EFI_HANDLE image,
                                        EFI_SYSTEM_TABLE *system_table,
                                        EFI_FILE_PROTOCOL **package_root);

#endif
