#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "openconsole/game_storage.h"

static int valid_slot(void *context,const oc_game_selection *candidate) {
    const uint8_t *valid=context;return valid[candidate->slot]!=0u;
}

int main(void) {
    oc_game_selection first,second,recovered,tombstone;
    uint8_t a[OC_GAME_SELECTION_BYTES],b[OC_GAME_SELECTION_BYTES],digest[32],inventory[32];
    uint8_t valid[2]={1u,1u};size_t i;
    for(i=0u;i<32u;++i){digest[i]=(uint8_t)i;inventory[i]=(uint8_t)(31u-i);}
    assert(oc_game_selection_next(0,"orbit","1.0.0",digest,inventory,100u,0,&first)==OC_GAME_STORAGE_OK);
    assert(first.generation==1u&&first.slot==0u&&first.selector==0u);
    assert(oc_game_selection_encode(&first,a)==OC_GAME_STORAGE_OK);
    assert(oc_game_selection_decode(a,0u,&recovered)==OC_GAME_STORAGE_OK);
    assert(strcmp(recovered.game_id,"orbit")==0&&strcmp(recovered.version,"1.0.0")==0);
    assert(oc_game_selection_next(&first,"orbit","1.1.0",inventory,digest,120u,0,&second)==OC_GAME_STORAGE_OK);
    assert(oc_game_selection_encode(&second,b)==OC_GAME_STORAGE_OK);
    assert(oc_game_selection_recover(a,sizeof(a),b,sizeof(b),valid_slot,valid,&recovered)==OC_GAME_STORAGE_OK);
    assert(recovered.generation==2u&&recovered.slot==1u);
    valid[1]=0u;
    assert(oc_game_selection_recover(a,sizeof(a),b,sizeof(b),valid_slot,valid,&recovered)==OC_GAME_STORAGE_OK);
    assert(recovered.generation==1u);
    b[17]^=1u;
    assert(oc_game_selection_decode(b,1u,&recovered)==OC_GAME_STORAGE_INVALID);
    assert(oc_game_selection_next(&first,"orbit","1.0.0",digest,inventory,0u,1,&tombstone)==OC_GAME_STORAGE_OK);
    assert(oc_game_selection_encode(&tombstone,b)==OC_GAME_STORAGE_OK);
    valid[1]=0u;
    assert(oc_game_selection_recover(a,sizeof(a),b,sizeof(b),valid_slot,valid,&recovered)==OC_GAME_STORAGE_OK);
    assert(recovered.tombstone&&recovered.generation==2u);
    first.generation=UINT64_MAX;
    assert(oc_game_selection_next(&first,"orbit","1.0.0",digest,inventory,1u,0,&second)==OC_GAME_STORAGE_OVERFLOW);
    puts("PASS: checksummed two-slot game selections, fallback, tombstone, and overflow");
    return 0;
}
