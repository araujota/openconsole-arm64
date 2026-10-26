#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "openconsole/game_storage.h"

#define FILE_CAPACITY 1024u
#define FILE_COUNT 16u

typedef struct {
    char path[OC_GAME_STORAGE_PATH_MAX];
    uint8_t data[FILE_CAPACITY];
    size_t size;
    int exists;
} mock_file;

typedef struct {
    mock_file files[FILE_COUNT];
    int fail_write;
    int fail_flush;
    int fail_digest;
    int fail_delete;
    int short_write;
    int tear_selector;
    size_t writes;
    size_t deletes;
    int fault_kind;
    size_t fault_nth;
    size_t fault_count[8];
} fixture;

enum {FAULT_NONE,FAULT_WRITE,FAULT_SHORT_WRITE,FAULT_FLUSH,FAULT_READ,
      FAULT_DIGEST,FAULT_DELETE,FAULT_VALIDATE};
static int fault(fixture*f,int kind){if(f->fault_kind!=kind)return 0;return++f->fault_count[kind]==f->fault_nth;}

static void digest_bytes(const uint8_t *data,size_t size,uint8_t out[32]) {
    size_t i;memset(out,0,32u);
    for(i=0u;i<size;++i) {
        out[i%32u]^=(uint8_t)(data[i]+(uint8_t)i);
        out[(i*7u)%32u]^=(uint8_t)(data[i]*3u+1u);
    }
}

static mock_file *find_file(fixture *f,const char *path,int create) {
    size_t i;mock_file *empty=0;
    for(i=0u;i<FILE_COUNT;++i) {
        if(f->files[i].exists&&strcmp(f->files[i].path,path)==0)return &f->files[i];
        if(empty==0&&!f->files[i].exists)empty=&f->files[i];
    }
    if(!create||empty==0)return 0;
    assert(strlen(path)<sizeof(empty->path));strcpy(empty->path,path);empty->exists=1;
    return empty;
}

static int write_file(void *context,const char *path,const uint8_t *data,size_t size) {
    fixture *f=context;mock_file *file;
    ++f->writes;if(f->fail_write||fault(f,FAULT_WRITE)||size>FILE_CAPACITY)return 0;
    file=find_file(f,path,1);if(file==0)return 0;
    if(fault(f,FAULT_SHORT_WRITE)){if(size)--size;memcpy(file->data,data,size);file->size=size;return 0;}
    if((f->short_write||(f->tear_selector&&strstr(path,"select-")!=0))&&size!=0u)--size;
    memcpy(file->data,data,size);file->size=size;return 1;
}
static int flush_file(void *context,const char *path) {
    fixture *f=context;(void)path;return !f->fail_flush&&!fault(f,FAULT_FLUSH);
}
static int read_file(void *context,const char *path,uint8_t *data,size_t capacity,size_t *size) {
    fixture *f=context;mock_file *file;if(fault(f,FAULT_READ))return-1;file=find_file(f,path,0);
    if(file==0||file->size>capacity)return 0;
    memcpy(data,file->data,file->size);*size=file->size;return 1;
}
static int digest_file(void *context,const char *path,uint64_t *size,uint8_t out[32]) {
    fixture *f=context;mock_file *file=find_file(f,path,0);
    if(f->fail_digest||fault(f,FAULT_DIGEST)||file==0)return 0;
    *size=file->size;digest_bytes(file->data,file->size,out);return 1;
}
static int delete_file(void *context,const char *path) {
    fixture *f=context;mock_file *file=find_file(f,path,0);++f->deletes;
    if(f->fail_delete||fault(f,FAULT_DELETE))return 0;
    if(file!=0){file->exists=0;file->size=0u;file->path[0]='\0';}return 1;
}
static int cleanup_slot(void *context,const char *root,size_t maximum,
                        size_t *deleted,int *complete) {
    fixture *f=context;size_t i,prefix=strlen(root),count=0u;
    if(f->fail_delete)return 0;
    for(i=0u;i<FILE_COUNT&&count<maximum;++i) {
        if(f->files[i].exists&&strncmp(f->files[i].path,root,prefix)==0&&
           (f->files[i].path[prefix]=='\\'||f->files[i].path[prefix]=='\0')) {
            f->files[i].exists=0;f->files[i].size=0;f->files[i].path[0]='\0';++count;++f->deletes;
        }
    }
    *complete=1;
    for(i=0u;i<FILE_COUNT;++i)if(f->files[i].exists&&strncmp(f->files[i].path,root,prefix)==0&&
       (f->files[i].path[prefix]=='\\'||f->files[i].path[prefix]=='\0'))*complete=0;
    *deleted=count;return 1;
}
static int validate_slot(void *context,const char *bundle_path,const char *inventory_path,
                         const oc_game_selection *selection) {
    fixture *f=context;mock_file *bundle=find_file(f,bundle_path,0);
    mock_file *inventory=find_file(f,inventory_path,0);uint8_t digest[32];
    if(fault(f,FAULT_VALIDATE)||bundle==0||inventory==0||bundle->size!=selection->bundle_size||bundle->size<32u||
       memcmp(bundle->data,selection->bundle_id,32u)!=0)return 0;
    digest_bytes(inventory->data,inventory->size,digest);
    return memcmp(digest,selection->inventory_digest,32u)==0;
}
static oc_game_storage_ops operations(fixture *f) {
    oc_game_storage_ops ops;
    ops.context=f;ops.write_file=write_file;ops.flush_file=flush_file;
    ops.read_file=read_file;ops.digest_file=digest_file;ops.delete_file=delete_file;
    ops.cleanup_slot=cleanup_slot;
    ops.validate_slot=validate_slot;return ops;
}
static void make_artifacts(uint8_t seed,uint8_t bundle[64],uint8_t inventory[48],
                           uint8_t bundle_id[32],uint8_t bundle_hash[32],uint8_t inventory_hash[32]) {
    size_t i;for(i=0u;i<32u;++i)bundle_id[i]=(uint8_t)(seed+i);
    memcpy(bundle,bundle_id,32u);for(i=32u;i<64u;++i)bundle[i]=(uint8_t)(seed^i);
    for(i=0u;i<48u;++i)inventory[i]=(uint8_t)(seed+i*3u);
    digest_bytes(bundle,64u,bundle_hash);digest_bytes(inventory,48u,inventory_hash);
}

int main(void) {
    fixture f,baseline,trial;oc_game_storage_ops ops;oc_game_selection first,second,recovered,tombstone;
    uint8_t bundle1[64],bundle2[64],inventory1[48],inventory2[48];
    uint8_t id1[32],id2[32],bundle_hash1[32],bundle_hash2[32],inventory_hash1[32],inventory_hash2[32];
    oc_game_install_file files1[1],files2[1];
    char path[OC_GAME_STORAGE_PATH_MAX];mock_file *file;size_t cleaned=0u;
    memset(&f,0,sizeof(f));ops=operations(&f);
    make_artifacts(0x10u,bundle1,inventory1,id1,bundle_hash1,inventory_hash1);
    make_artifacts(0x40u,bundle2,inventory2,id2,bundle_hash2,inventory_hash2);
    files1[0].path="game.toml";files1[0].data=inventory1;files1[0].size=sizeof(inventory1);files1[0].sha256=inventory_hash1;
    files2[0].path="game.toml";files2[0].data=inventory2;files2[0].size=sizeof(inventory2);files2[0].sha256=inventory_hash2;

    assert(oc_game_storage_path("game",OC_GAME_PATH_BUNDLE,0u,path,sizeof(path))==OC_GAME_STORAGE_OK);
    assert(strcmp(path,"\\EFI\\OpenConsole\\State\\Games\\game\\slot-a\\bundle.ocg")==0);
    assert(oc_game_storage_path("../game",OC_GAME_PATH_BUNDLE,0u,path,sizeof(path))==OC_GAME_STORAGE_INVALID);
    assert(oc_game_stage_and_commit(&ops,"game","1.0.0",bundle1,sizeof(bundle1),bundle_hash1,
                                    id1,inventory1,sizeof(inventory1),inventory_hash1,files1,1u,&first)==OC_GAME_STORAGE_OK);
    assert(first.generation==1u&&first.slot==0u&&first.selector==0u);
    assert(oc_game_transaction_recover(&ops,"game",0u,&cleaned,&recovered)==OC_GAME_STORAGE_OK);
    assert(recovered.generation==1u&&recovered.slot==0u);
    baseline=f;

    /* Every fallible install operation preserves the committed selector. */
    {
        static const struct {int kind;size_t count;} matrix[]={
            {FAULT_WRITE,5u},{FAULT_SHORT_WRITE,5u},{FAULT_FLUSH,5u},
            {FAULT_READ,3u},{FAULT_DIGEST,3u}};
        size_t m,n;
        for(m=0u;m<sizeof(matrix)/sizeof(matrix[0]);++m)for(n=1u;n<=matrix[m].count;++n){
            trial=baseline;memset(trial.fault_count,0,sizeof(trial.fault_count));
            trial.fault_kind=matrix[m].kind;trial.fault_nth=n;ops=operations(&trial);
            assert(oc_game_stage_and_commit(&ops,"game","2.0.0",bundle2,sizeof(bundle2),bundle_hash2,
                id2,inventory2,sizeof(inventory2),inventory_hash2,files2,1u,&second)!=OC_GAME_STORAGE_OK);
            trial.fault_kind=FAULT_NONE;ops=operations(&trial);
            assert(oc_game_transaction_recover(&ops,"game",0u,0,&recovered)==OC_GAME_STORAGE_OK);
            assert((recovered.generation==1u&&recovered.slot==0u)||
                   (recovered.generation==2u&&recovered.slot==1u&&
                    memcmp(recovered.bundle_id,id2,32u)==0));
        }
        trial=baseline;trial.fault_kind=FAULT_VALIDATE;trial.fault_nth=2u;
        ops=operations(&trial);
        assert(oc_game_stage_and_commit(&ops,"game","2.0.0",bundle2,sizeof(bundle2),bundle_hash2,
            id2,inventory2,sizeof(inventory2),inventory_hash2,files2,1u,&second)==OC_GAME_STORAGE_VERIFY);
        trial.fault_kind=FAULT_NONE;ops=operations(&trial);
        assert(oc_game_transaction_recover(&ops,"game",0u,0,&recovered)==OC_GAME_STORAGE_OK&&
               recovered.generation==1u);
        {
            static const struct {int kind;size_t count;} removal[]={
                {FAULT_WRITE,1u},{FAULT_FLUSH,1u},{FAULT_READ,3u}};
            for(m=0u;m<sizeof(removal)/sizeof(removal[0]);++m)
                for(n=1u;n<=removal[m].count;++n){
                    trial=baseline;memset(trial.fault_count,0,sizeof(trial.fault_count));
                    trial.fault_kind=removal[m].kind;trial.fault_nth=n;ops=operations(&trial);
                    assert(oc_game_tombstone_and_cleanup(&ops,"game",1,
                        OC_GAME_GC_MAX_DELETES,&cleaned,&tombstone)!=OC_GAME_STORAGE_OK);
                    trial.fault_kind=FAULT_NONE;ops=operations(&trial);
                    assert(oc_game_transaction_recover(&ops,"game",0u,0,&recovered)==OC_GAME_STORAGE_OK);
                    assert((recovered.generation==1u&&!recovered.tombstone)||
                           (recovered.generation==2u&&recovered.tombstone));
                }
        }
    }
    f=baseline;ops=operations(&f);

    /* Failed staging never displaces the last-known-good selector. */
    f.fail_flush=1;
    assert(oc_game_stage_and_commit(&ops,"game","2.0.0",bundle2,sizeof(bundle2),bundle_hash2,
                                    id2,inventory2,sizeof(inventory2),inventory_hash2,files2,1u,&second)==OC_GAME_STORAGE_FLUSH);
    f.fail_flush=0;assert(oc_game_transaction_recover(&ops,"game",0u,0,&recovered)==OC_GAME_STORAGE_OK);
    assert(recovered.generation==1u&&recovered.slot==0u);
    f.short_write=1;
    assert(oc_game_stage_and_commit(&ops,"game","2.0.0",bundle2,sizeof(bundle2),bundle_hash2,
                                    id2,inventory2,sizeof(inventory2),inventory_hash2,files2,1u,&second)==OC_GAME_STORAGE_VERIFY);
    f.short_write=0;

    assert(oc_game_stage_and_commit(&ops,"game","2.0.0",bundle2,sizeof(bundle2),bundle_hash2,
                                    id2,inventory2,sizeof(inventory2),inventory_hash2,files2,1u,&second)==OC_GAME_STORAGE_OK);
    assert(second.generation==2u&&second.slot==1u&&second.selector==1u);
    /* Corruption of the latest selected slot falls back to generation one. */
    assert(oc_game_storage_path("game",OC_GAME_PATH_BUNDLE,1u,path,sizeof(path))==OC_GAME_STORAGE_OK);
    file=find_file(&f,path,0);assert(file);file->data[0]^=1u;
    assert(oc_game_transaction_recover(&ops,"game",0u,0,&recovered)==OC_GAME_STORAGE_OK);
    assert(recovered.generation==1u&&recovered.slot==0u);file->data[0]^=1u;

    /* A torn new selector leaves generation two selected. */
    f.tear_selector=1;
    assert(oc_game_stage_and_commit(&ops,"game","3.0.0",bundle1,sizeof(bundle1),bundle_hash1,
                                    id1,inventory1,sizeof(inventory1),inventory_hash1,files1,1u,&recovered)==OC_GAME_STORAGE_VERIFY);
    f.tear_selector=0;assert(oc_game_transaction_recover(&ops,"game",0u,0,&recovered)==OC_GAME_STORAGE_OK);
    assert(recovered.generation==2u&&recovered.slot==1u);

    /* Recovery deletes only known inactive paths and is explicitly bounded. */
    assert(oc_game_transaction_recover(&ops,"game",2u,&cleaned,&recovered)==OC_GAME_STORAGE_CLEANUP);
    assert(cleaned==2u);
    assert(oc_game_transaction_recover(&ops,"game",OC_GAME_GC_MAX_DELETES,&cleaned,&recovered)==OC_GAME_STORAGE_OK);
    assert(cleaned>=1u);

    /* Tombstone commits before cleanup. A cleanup failure cannot resurrect it. */
    f.fail_delete=1;
    assert(oc_game_tombstone_and_cleanup(&ops,"game",1,OC_GAME_GC_MAX_DELETES,&cleaned,&tombstone)==
           OC_GAME_STORAGE_CLEANUP);
    assert(tombstone.tombstone&&tombstone.generation==3u);
    assert(oc_game_transaction_recover(&ops,"game",0u,0,&recovered)==OC_GAME_STORAGE_OK&&recovered.tombstone);
    f.fail_delete=0;
    assert(oc_game_tombstone_and_cleanup(&ops,"game",1,OC_GAME_GC_MAX_DELETES,&cleaned,&tombstone)==
           OC_GAME_STORAGE_OK);
    assert(cleaned>=1u);
    assert(oc_game_tombstone_and_cleanup(&ops,"../game",1,1u,&cleaned,&tombstone)==OC_GAME_STORAGE_INVALID);
    assert(oc_game_tombstone_and_cleanup(&ops,"game",0,1u,&cleaned,&tombstone)==OC_GAME_STORAGE_INVALID);
    puts("PASS: confined staged game install, recovery, tombstone, and bounded cleanup");
    return 0;
}
