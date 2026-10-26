/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OPENCONSOLE_LAUNCH_RECOVERY_H
#define OPENCONSOLE_LAUNCH_RECOVERY_H
#include <stddef.h>
#include <stdint.h>
#define OC_LAUNCH_RECORD_BYTES 196u
typedef enum { OC_LAUNCH_ATTEMPT=1, OC_LAUNCH_COMPLETE=2, OC_LAUNCH_QUARANTINE=3 } oc_launch_state;
typedef struct { uint64_t generation; uint8_t bundle_id[32]; char game_id[65]; char version[33]; uint8_t state; uint8_t slot; } oc_launch_record;
int oc_launch_record_encode(const oc_launch_record*,uint8_t[OC_LAUNCH_RECORD_BYTES]);
int oc_launch_record_decode(const uint8_t*,size_t,uint8_t,oc_launch_record*);
int oc_launch_record_recover(const uint8_t*,size_t,const uint8_t*,size_t,oc_launch_record*);
int oc_launch_record_next(const oc_launch_record*,const char*,const char*,const uint8_t[32],oc_launch_state,oc_launch_record*);
int oc_launch_record_quarantines(const oc_launch_record*,const uint8_t[32]);
#endif
