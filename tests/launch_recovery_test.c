#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "openconsole/launch_recovery.h"
int main(void){uint8_t a[196],b[196],id[32]={1};oc_launch_record one,two,r;assert(oc_launch_record_next(0,"game","1.0.0",id,OC_LAUNCH_ATTEMPT,&one));assert(oc_launch_record_encode(&one,a));assert(oc_launch_record_recover(a,sizeof(a),0,0,&r)&&oc_launch_record_quarantines(&r,id));assert(oc_launch_record_next(&one,"game","1.0.0",id,OC_LAUNCH_COMPLETE,&two));assert(oc_launch_record_encode(&two,b));assert(oc_launch_record_recover(a,sizeof(a),b,sizeof(b),&r)&&r.state==OC_LAUNCH_COMPLETE&&!oc_launch_record_quarantines(&r,id));b[12]^=1;assert(oc_launch_record_recover(a,sizeof(a),b,sizeof(b),&r)&&r.generation==1);a[0]^=1;assert(!oc_launch_record_recover(a,sizeof(a),b,sizeof(b),&r));assert(!oc_launch_record_next(0,"../bad","1",id,OC_LAUNCH_ATTEMPT,&r));puts("PASS: checksummed two-slot launch attempt, completion, and quarantine records");}
