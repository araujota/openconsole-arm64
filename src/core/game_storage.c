#include "openconsole/game_storage.h"

static uint32_t crc32(const uint8_t *bytes, size_t size) {
    uint32_t crc = 0xffffffffu;
    size_t i;
    for (i = 0u; i < size; ++i) {
        uint32_t bit;
        crc ^= bytes[i];
        for (bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
static void put32(uint8_t *out, uint32_t value) {
    out[0]=(uint8_t)value; out[1]=(uint8_t)(value>>8u);
    out[2]=(uint8_t)(value>>16u); out[3]=(uint8_t)(value>>24u);
}
static uint32_t get32(const uint8_t *in) {
    return (uint32_t)in[0]|((uint32_t)in[1]<<8u)|((uint32_t)in[2]<<16u)|
           ((uint32_t)in[3]<<24u);
}
static void put64(uint8_t *out, uint64_t value) {
    uint32_t i; for(i=0u;i<8u;++i) out[i]=(uint8_t)(value>>(i*8u));
}
static uint64_t get64(const uint8_t *in) {
    uint64_t value=0u; uint32_t i;
    for(i=0u;i<8u;++i) value|=(uint64_t)in[i]<<(i*8u);
    return value;
}
static void copy(void *destination, const void *source, size_t size) {
    uint8_t *out=destination; const uint8_t *in=source;
    while(size--!=0u)*out++=*in++;
}
static void zero(void *destination, size_t size) {
    uint8_t *out=destination; while(size--!=0u)*out++=0u;
}
static size_t bounded_length(const char *value, size_t maximum) {
    size_t size=0u; if(value==0)return maximum+1u;
    while(size<=maximum&&value[size]!='\0')++size;
    return size;
}
static int id_valid(const char *value, size_t size) {
    size_t i;
    if(size==0u||size>OC_GAME_ID_MAX||
       !((value[0]>='a'&&value[0]<='z')||(value[0]>='0'&&value[0]<='9')))return 0;
    for(i=1u;i<size;++i)if(!((value[i]>='a'&&value[i]<='z')||
        (value[i]>='0'&&value[i]<='9')||value[i]=='-'||value[i]=='_'))return 0;
    return 1;
}
static int version_valid(const char *value, size_t size) {
    size_t i;
    if(size==0u||size>OC_GAME_VERSION_MAX)return 0;
    for(i=0u;i<size;++i)if(!((value[i]>='A'&&value[i]<='Z')||
       (value[i]>='a'&&value[i]<='z')||(value[i]>='0'&&value[i]<='9')||
       value[i]=='.'||value[i]=='+'||value[i]=='_'||value[i]=='-'))return 0;
    return 1;
}
static int equal(const uint8_t *a,const uint8_t *b,size_t size) {
    uint8_t difference=0u;while(size--!=0u)difference|=(uint8_t)(*a++^*b++);
    return difference==0u;
}
static int append(char *out,size_t capacity,size_t *used,const char *text) {
    size_t i=0u;while(text[i]!='\0')++i;
    if(*used>=capacity||i>=capacity-*used)return 0;
    copy(out+*used,text,i);*used+=i;out[*used]='\0';return 1;
}

oc_game_storage_result oc_game_storage_path(
    const char *game_id,oc_game_path_kind kind,uint8_t index,
    char *out,size_t capacity) {
    size_t used=0u,id_size=bounded_length(game_id,OC_GAME_ID_MAX);
    const char *suffix;
    if(out==0||capacity==0u||index>1u||!id_valid(game_id,id_size)||
       kind<OC_GAME_PATH_BUNDLE||kind>OC_GAME_PATH_SLOT)
        return OC_GAME_STORAGE_INVALID;
    out[0]='\0';
    if(!append(out,capacity,&used,"\\EFI\\OpenConsole\\State\\Games\\")||
       !append(out,capacity,&used,game_id))return OC_GAME_STORAGE_OVERFLOW;
    if(kind==OC_GAME_PATH_SELECTOR)
        suffix=index==0u?"\\select-a.ocgs":"\\select-b.ocgs";
    else if(kind==OC_GAME_PATH_SLOT)
        suffix=index==0u?"\\slot-a":"\\slot-b";
    else if(kind==OC_GAME_PATH_BUNDLE)
        suffix=index==0u?"\\slot-a\\bundle.ocg":"\\slot-b\\bundle.ocg";
    else if(kind==OC_GAME_PATH_DESCRIPTOR)
        suffix=index==0u?"\\slot-a\\descriptor.ocgi":"\\slot-b\\descriptor.ocgi";
    else suffix=index==0u?"\\slot-a\\staging.ocgt":"\\slot-b\\staging.ocgt";
    return append(out,capacity,&used,suffix)?OC_GAME_STORAGE_OK:OC_GAME_STORAGE_OVERFLOW;
}

oc_game_storage_result oc_game_package_path(const char *game_id,uint8_t slot,
    const char *relative,char *out,size_t capacity) {
    size_t used=0u,id_size=bounded_length(game_id,OC_GAME_ID_MAX),i,start=0u,n;
    if(!out||!capacity||slot>1u||!id_valid(game_id,id_size)||!relative)
        return OC_GAME_STORAGE_INVALID;
    n=bounded_length(relative,OC_GAME_STORAGE_PATH_MAX);
    if(n==0u||n>=OC_GAME_STORAGE_PATH_MAX||relative[0]=='/'||relative[0]=='\\')
        return OC_GAME_STORAGE_INVALID;
    for(i=0u;i<n;++i) {
        unsigned char c=(unsigned char)relative[i];
        if(c>=0x80u||c=='\\'||c<0x20u)return OC_GAME_STORAGE_INVALID;
        if(c=='/') {
            if(i==start||(i-start==1u&&relative[start]=='.')||
               (i-start==2u&&relative[start]=='.'&&relative[start+1u]=='.'))
                return OC_GAME_STORAGE_INVALID;
            start=i+1u;
        }
    }
    if(start==n||(n-start==1u&&relative[start]=='.')||
       (n-start==2u&&relative[start]=='.'&&relative[start+1u]=='.'))
        return OC_GAME_STORAGE_INVALID;
    out[0]='\0';
    if(!append(out,capacity,&used,"\\EFI\\OpenConsole\\State\\Games\\")||
       !append(out,capacity,&used,game_id)||
       !append(out,capacity,&used,slot==0u?"\\slot-a\\package\\":"\\slot-b\\package\\")||
       !append(out,capacity,&used,relative))return OC_GAME_STORAGE_OVERFLOW;
    for(i=0u;out[i]!='\0';++i)if(out[i]=='/')out[i]='\\';
    return OC_GAME_STORAGE_OK;
}

oc_game_storage_result oc_game_selection_encode(
    const oc_game_selection *value, uint8_t out[OC_GAME_SELECTION_BYTES]) {
    size_t id_size, version_size;
    if(value==0||out==0||value->generation==0u||value->slot>1u||
       value->selector>1u||value->tombstone>1u)return OC_GAME_STORAGE_INVALID;
    id_size=bounded_length(value->game_id,OC_GAME_ID_MAX);
    version_size=bounded_length(value->version,OC_GAME_VERSION_MAX);
    if(!id_valid(value->game_id,id_size)||!version_valid(value->version,version_size)||
       (!value->tombstone&&value->bundle_size==0u))return OC_GAME_STORAGE_INVALID;
    zero(out,OC_GAME_SELECTION_BYTES);
    out[0]='O';out[1]='C';out[2]='G';out[3]='S';out[4]=1u;
    out[5]=value->slot;out[6]=value->tombstone;
    put64(out+8u,value->generation);copy(out+16u,value->bundle_id,32u);
    copy(out+48u,value->inventory_digest,32u);put64(out+80u,value->bundle_size);
    out[88]=(uint8_t)id_size;out[89]=(uint8_t)version_size;
    copy(out+96u,value->game_id,id_size);copy(out+160u,value->version,version_size);
    put32(out+192u,crc32(out,192u));return OC_GAME_STORAGE_OK;
}

oc_game_storage_result oc_game_selection_decode(
    const uint8_t in[OC_GAME_SELECTION_BYTES], uint8_t selector,
    oc_game_selection *value) {
    size_t i,id_size,version_size;
    if(in==0||value==0||selector>1u||in[0]!='O'||in[1]!='C'||in[2]!='G'||
       in[3]!='S'||in[4]!=1u||in[5]>1u||in[6]>1u||in[7]!=0u||
       get32(in+192u)!=crc32(in,192u))return OC_GAME_STORAGE_INVALID;
    for(i=90u;i<96u;++i)if(in[i]!=0u)return OC_GAME_STORAGE_INVALID;
    id_size=in[88];version_size=in[89];
    if(!id_valid((const char *)(in+96u),id_size)||
       !version_valid((const char *)(in+160u),version_size))return OC_GAME_STORAGE_INVALID;
    for(i=96u+id_size;i<160u;++i)if(in[i]!=0u)return OC_GAME_STORAGE_INVALID;
    for(i=160u+version_size;i<192u;++i)if(in[i]!=0u)return OC_GAME_STORAGE_INVALID;
    zero(value,sizeof(*value));value->slot=in[5];value->selector=selector;
    value->tombstone=in[6];value->generation=get64(in+8u);
    value->bundle_size=get64(in+80u);
    if(value->generation==0u||(!value->tombstone&&value->bundle_size==0u))
        return OC_GAME_STORAGE_INVALID;
    copy(value->bundle_id,in+16u,32u);copy(value->inventory_digest,in+48u,32u);
    copy(value->game_id,in+96u,id_size);value->game_id[id_size]='\0';
    copy(value->version,in+160u,version_size);value->version[version_size]='\0';
    return OC_GAME_STORAGE_OK;
}

oc_game_storage_result oc_game_selection_recover(
    const uint8_t *record_a,size_t size_a,const uint8_t *record_b,size_t size_b,
    oc_game_slot_validator validator,void *context,oc_game_selection *selected) {
    oc_game_selection candidate,best;int found=0;
    if(selected==0||validator==0)return OC_GAME_STORAGE_INVALID;
    if(size_a==OC_GAME_SELECTION_BYTES&&
       oc_game_selection_decode(record_a,0u,&candidate)==OC_GAME_STORAGE_OK&&
       (candidate.tombstone||validator(context,&candidate))) {best=candidate;found=1;}
    if(size_b==OC_GAME_SELECTION_BYTES&&
       oc_game_selection_decode(record_b,1u,&candidate)==OC_GAME_STORAGE_OK&&
       (candidate.tombstone||validator(context,&candidate))&&
       (!found||candidate.generation>best.generation)) {best=candidate;found=1;}
    if(!found)return OC_GAME_STORAGE_NO_SELECTION;
    *selected=best;
    return OC_GAME_STORAGE_OK;
}

oc_game_storage_result oc_game_selection_next(
    const oc_game_selection *current,const char *game_id,const char *version,
    const uint8_t bundle_id[32],const uint8_t inventory_digest[32],
    uint64_t bundle_size,int tombstone,oc_game_selection *next) {
    size_t id_size=bounded_length(game_id,OC_GAME_ID_MAX);
    size_t version_size=bounded_length(version,OC_GAME_VERSION_MAX);
    if(next==0||bundle_id==0||inventory_digest==0||!id_valid(game_id,id_size)||
       !version_valid(version,version_size)||tombstone<0||tombstone>1||
       (!tombstone&&bundle_size==0u))return OC_GAME_STORAGE_INVALID;
    zero(next,sizeof(*next));next->generation=current?current->generation+1u:1u;
    if(next->generation==0u)return OC_GAME_STORAGE_OVERFLOW;
    next->slot=current?(uint8_t)(1u-current->slot):0u;
    next->selector=current?(uint8_t)(1u-current->selector):0u;
    next->tombstone=(uint8_t)tombstone;next->bundle_size=bundle_size;
    copy(next->bundle_id,bundle_id,32u);copy(next->inventory_digest,inventory_digest,32u);
    copy(next->game_id,game_id,id_size+1u);copy(next->version,version,version_size+1u);
    return OC_GAME_STORAGE_OK;
}

static int string_equal(const char *a,const char *b) {
    size_t i=0u;if(a==0||b==0)return 0;
    while(a[i]!='\0'&&b[i]!='\0'){if(a[i]!=b[i])return 0;++i;}
    return a[i]==b[i];
}

static int make_slot_paths(const char *game_id,uint8_t slot,
                           char bundle[OC_GAME_STORAGE_PATH_MAX],
                           char inventory[OC_GAME_STORAGE_PATH_MAX]) {
    return oc_game_storage_path(game_id,OC_GAME_PATH_BUNDLE,slot,bundle,
                                OC_GAME_STORAGE_PATH_MAX)==OC_GAME_STORAGE_OK&&
           oc_game_storage_path(game_id,OC_GAME_PATH_DESCRIPTOR,slot,inventory,
                                OC_GAME_STORAGE_PATH_MAX)==OC_GAME_STORAGE_OK;
}

static int transaction_candidate(const oc_game_storage_ops *ops,const char *game_id,
                                 const uint8_t *record,size_t record_size,uint8_t selector,
                                 oc_game_selection *candidate) {
    char bundle[OC_GAME_STORAGE_PATH_MAX],inventory[OC_GAME_STORAGE_PATH_MAX];
    if(record_size!=OC_GAME_SELECTION_BYTES||
       oc_game_selection_decode(record,selector,candidate)!=OC_GAME_STORAGE_OK||
       !string_equal(candidate->game_id,game_id))return 0;
    if(candidate->tombstone)return 1;
    if(!make_slot_paths(game_id,candidate->slot,bundle,inventory))return 0;
    return ops->validate_slot(ops->context,bundle,inventory,candidate);
}

static oc_game_storage_result recover_without_cleanup(
    const oc_game_storage_ops *ops,const char *game_id,oc_game_selection *selected) {
    uint8_t records[2][OC_GAME_SELECTION_BYTES],index;size_t sizes[2]={0u,0u};
    size_t game_id_size=bounded_length(game_id,OC_GAME_ID_MAX);
    char path[OC_GAME_STORAGE_PATH_MAX];oc_game_selection candidate,best;int found=0;
    if(ops==0||game_id==0||selected==0||ops->read_file==0||
       ops->validate_slot==0||!id_valid(game_id,game_id_size))return OC_GAME_STORAGE_INVALID;
    for(index=0u;index<2u;++index) {
        int read_result;
        sizes[index]=sizeof(records[index]);
        if(oc_game_storage_path(game_id,OC_GAME_PATH_SELECTOR,index,path,sizeof(path))!=
           OC_GAME_STORAGE_OK)return OC_GAME_STORAGE_INVALID;
        read_result=ops->read_file(ops->context,path,records[index],
                                   sizeof(records[index]),&sizes[index]);
        if(read_result<0)return OC_GAME_STORAGE_IO;
        if(read_result==0)
            sizes[index]=0u;
        if(transaction_candidate(ops,game_id,records[index],sizes[index],index,&candidate)&&
           (!found||candidate.generation>best.generation)) {
            best=candidate;found=1;
        }
    }
    if(!found)return OC_GAME_STORAGE_NO_SELECTION;
    *selected=best;return OC_GAME_STORAGE_OK;
}

static oc_game_storage_result cleanup_slots(
    const oc_game_storage_ops *ops,const char *game_id,const oc_game_selection *selected,
    size_t maximum,size_t *deleted) {
    char path[OC_GAME_STORAGE_PATH_MAX];uint8_t slot;
    size_t attempts=0u;int incomplete=0;
    if(deleted!=0)*deleted=0u;
    if(ops->delete_file==0||ops->cleanup_slot==0)
        return maximum==0u?OC_GAME_STORAGE_OK:OC_GAME_STORAGE_INVALID;
    for(slot=0u;slot<2u;++slot) {
        if(!selected->tombstone&&slot==selected->slot)continue;
        if(attempts>=maximum){incomplete=1;continue;}
        else {
            size_t removed=0u;int complete=0;
            if(oc_game_storage_path(game_id,OC_GAME_PATH_SLOT,slot,path,sizeof(path))!=OC_GAME_STORAGE_OK)
                return OC_GAME_STORAGE_INVALID;
            if(!ops->cleanup_slot(ops->context,path,maximum-attempts,&removed,&complete))
                incomplete=1;
            if(removed>maximum-attempts)return OC_GAME_STORAGE_INVALID;
            attempts+=removed;if(!complete)incomplete=1;
        }
    }
    /* A selected slot can retain only an interrupted staging marker. */
    if(!selected->tombstone) {
        if(attempts>=maximum)incomplete=1;
        else {
            if(oc_game_storage_path(game_id,OC_GAME_PATH_STAGING,selected->slot,path,
                                    sizeof(path))!=OC_GAME_STORAGE_OK)
                return OC_GAME_STORAGE_INVALID;
            ++attempts;if(!ops->delete_file(ops->context,path))incomplete=1;
        }
    }
    if(deleted!=0)*deleted=attempts;
    return incomplete?OC_GAME_STORAGE_CLEANUP:OC_GAME_STORAGE_OK;
}

oc_game_storage_result oc_game_transaction_recover(
    const oc_game_storage_ops *ops,const char *game_id,size_t maximum_cleanup,
    size_t *cleanup_deletes,oc_game_selection *selected) {
    oc_game_storage_result result;
    if(maximum_cleanup>OC_GAME_GC_MAX_DELETES)return OC_GAME_STORAGE_INVALID;
    if(cleanup_deletes!=0)*cleanup_deletes=0u;
    result=recover_without_cleanup(ops,game_id,selected);
    if(result!=OC_GAME_STORAGE_OK||maximum_cleanup==0u)return result;
    return cleanup_slots(ops,game_id,selected,maximum_cleanup,cleanup_deletes);
}

static oc_game_storage_result commit_selector(
    const oc_game_storage_ops *ops,const char *game_id,const oc_game_selection *next) {
    uint8_t record[OC_GAME_SELECTION_BYTES],persisted[OC_GAME_SELECTION_BYTES];
    char path[OC_GAME_STORAGE_PATH_MAX];size_t read_size=sizeof(persisted);
    oc_game_selection check;
    if(oc_game_selection_encode(next,record)!=OC_GAME_STORAGE_OK||
       oc_game_storage_path(game_id,OC_GAME_PATH_SELECTOR,next->selector,path,sizeof(path))!=
       OC_GAME_STORAGE_OK)return OC_GAME_STORAGE_INVALID;
    if(!ops->write_file(ops->context,path,record,sizeof(record)))return OC_GAME_STORAGE_IO;
    if(!ops->flush_file(ops->context,path))return OC_GAME_STORAGE_FLUSH;
    {int read_result=ops->read_file(ops->context,path,persisted,sizeof(persisted),&read_size);
    if(read_result<0)return OC_GAME_STORAGE_IO;
    if(read_result==0||
       read_size!=sizeof(persisted)||
       oc_game_selection_decode(persisted,next->selector,&check)!=OC_GAME_STORAGE_OK||
       check.generation!=next->generation||check.slot!=next->slot||
       check.tombstone!=next->tombstone||check.bundle_size!=next->bundle_size||
       !string_equal(check.game_id,next->game_id)||
       !string_equal(check.version,next->version)||
       !equal(check.bundle_id,next->bundle_id,32u)||
       !equal(check.inventory_digest,next->inventory_digest,32u))
        return OC_GAME_STORAGE_VERIFY;}
    return OC_GAME_STORAGE_OK;
}

oc_game_storage_result oc_game_stage_and_commit(
    const oc_game_storage_ops *ops,const char *game_id,const char *version,
    const uint8_t *bundle,size_t bundle_size,const uint8_t bundle_sha256[32],
    const uint8_t bundle_id[32],const uint8_t *descriptor,size_t descriptor_size,
    const uint8_t descriptor_sha256[32],const oc_game_install_file *files,
    size_t file_count,oc_game_selection *committed) {
    oc_game_selection current,next;oc_game_storage_result recovered,result;
    char bundle_path[OC_GAME_STORAGE_PATH_MAX],inventory_path[OC_GAME_STORAGE_PATH_MAX];
    char staging_path[OC_GAME_STORAGE_PATH_MAX];uint8_t marker[48],digest[32];
    uint64_t persisted_size=0u,required_bytes=0u,available_bytes=0u;
    size_t i,j,total_files=0u;
    if(ops==0||game_id==0||version==0||bundle==0||bundle_sha256==0||
       bundle_id==0||descriptor==0||descriptor_sha256==0||files==0||committed==0||
       bundle_size==0u||bundle_size>OC_GAME_BUNDLE_MAX||descriptor_size==0u||
       descriptor_size>OC_GAME_INVENTORY_MAX||file_count==0u||
       file_count>OC_GAME_INSTALL_MAX_FILES||ops->write_file==0||
       ops->flush_file==0||ops->read_file==0||ops->digest_file==0||
       ops->validate_slot==0||ops->available_bytes==0||
       (uint64_t)bundle_size>OC_FAT32_MAX_FILE_BYTES||
       (uint64_t)descriptor_size>OC_FAT32_MAX_FILE_BYTES)
        return OC_GAME_STORAGE_INVALID;
    for(i=0u;i<file_count;++i) {
        char checked[OC_GAME_STORAGE_PATH_MAX];
        size_t a,b;
        if(files[i].path==0||(files[i].data==0&&files[i].size!=0u)||files[i].sha256==0||
           files[i].size>32u*1024u*1024u||
           (uint64_t)files[i].size>OC_FAT32_MAX_FILE_BYTES||
           total_files>OC_GAME_BUNDLE_MAX-files[i].size||
           oc_game_package_path(game_id,0u,files[i].path,checked,sizeof(checked))!=OC_GAME_STORAGE_OK)
            return OC_GAME_STORAGE_INVALID;
        total_files+=files[i].size;
        for(j=0u;j<i;++j) {
            const char *left=files[i].path,*right=files[j].path;a=0u;b=0u;
            while(left[a]!='\0'&&right[b]!='\0') {
                char x=left[a],y=right[b];if(x>='A'&&x<='Z')x=(char)(x+32);if(y>='A'&&y<='Z')y=(char)(y+32);
                if(x!=y)break;
                ++a;++b;
            }
            if(left[a]=='\0'&&right[b]=='\0')return OC_GAME_STORAGE_INVALID;
        }
    }
    required_bytes=(uint64_t)bundle_size+(uint64_t)descriptor_size+
                   (uint64_t)total_files+48u+OC_GAME_SELECTION_BYTES;
    if(required_bytes<(uint64_t)bundle_size||
       !ops->available_bytes(ops->context,&available_bytes)||
       available_bytes<required_bytes)return OC_GAME_STORAGE_NO_SPACE;
    recovered=recover_without_cleanup(ops,game_id,&current);
    if(recovered!=OC_GAME_STORAGE_OK&&recovered!=OC_GAME_STORAGE_NO_SELECTION)return recovered;
    result=oc_game_selection_next(recovered==OC_GAME_STORAGE_OK?&current:0,game_id,
                                  version,bundle_id,descriptor_sha256,bundle_size,0,&next);
    if(result!=OC_GAME_STORAGE_OK)return result;
    if(!make_slot_paths(game_id,next.slot,bundle_path,inventory_path)||
       oc_game_storage_path(game_id,OC_GAME_PATH_STAGING,next.slot,staging_path,
                            sizeof(staging_path))!=OC_GAME_STORAGE_OK)
        return OC_GAME_STORAGE_INVALID;
    zero(marker,sizeof(marker));marker[0]='O';marker[1]='C';marker[2]='G';marker[3]='T';
    marker[4]=1u;marker[5]=next.slot;put64(marker+8u,next.generation);
    copy(marker+16u,bundle_sha256,32u);
    if(!ops->write_file(ops->context,staging_path,marker,sizeof(marker)))
        return OC_GAME_STORAGE_IO;
    if(!ops->flush_file(ops->context,staging_path))return OC_GAME_STORAGE_FLUSH;
    if(!ops->write_file(ops->context,bundle_path,bundle,bundle_size))
        return OC_GAME_STORAGE_IO;
    if(!ops->flush_file(ops->context,bundle_path))return OC_GAME_STORAGE_FLUSH;
    if(!ops->write_file(ops->context,inventory_path,descriptor,descriptor_size))
        return OC_GAME_STORAGE_IO;
    if(!ops->flush_file(ops->context,inventory_path))return OC_GAME_STORAGE_FLUSH;
    if(!ops->digest_file(ops->context,bundle_path,&persisted_size,digest)||
       persisted_size!=bundle_size||!equal(digest,bundle_sha256,32u))
        return OC_GAME_STORAGE_VERIFY;
    persisted_size=0u;zero(digest,sizeof(digest));
    if(!ops->digest_file(ops->context,inventory_path,&persisted_size,digest)||
       persisted_size!=descriptor_size||!equal(digest,descriptor_sha256,32u))
        return OC_GAME_STORAGE_VERIFY;
    for(i=0u;i<file_count;++i) {
        char package_path[OC_GAME_STORAGE_PATH_MAX];
        if(oc_game_package_path(game_id,next.slot,files[i].path,package_path,
                                sizeof(package_path))!=OC_GAME_STORAGE_OK)
            return OC_GAME_STORAGE_INVALID;
        if(!ops->write_file(ops->context,package_path,files[i].data,files[i].size))
            return OC_GAME_STORAGE_IO;
        if(!ops->flush_file(ops->context,package_path))return OC_GAME_STORAGE_FLUSH;
        persisted_size=0u;zero(digest,sizeof(digest));
        if(!ops->digest_file(ops->context,package_path,&persisted_size,digest)||
           persisted_size!=files[i].size||!equal(digest,files[i].sha256,32u))
            return OC_GAME_STORAGE_VERIFY;
    }
    if(!ops->validate_slot(ops->context,bundle_path,inventory_path,&next))
        return OC_GAME_STORAGE_VERIFY;
    result=commit_selector(ops,game_id,&next);
    if(result!=OC_GAME_STORAGE_OK)return result;
    *committed=next;
    /* The marker is not authoritative; interrupted cleanup is handled at boot. */
    if(ops->delete_file!=0)(void)ops->delete_file(ops->context,staging_path);
    for(i=0u;i<sizeof(marker);++i)marker[i]=0u;
    return OC_GAME_STORAGE_OK;
}

oc_game_storage_result oc_game_tombstone_and_cleanup(
    const oc_game_storage_ops *ops,const char *game_id,int confirmed,
    size_t maximum_cleanup,size_t *cleanup_deletes,oc_game_selection *committed) {
    oc_game_selection current,next;oc_game_storage_result result;
    if(confirmed!=1||ops==0||game_id==0||committed==0||
       ops->write_file==0||ops->flush_file==0||ops->read_file==0||
       ops->validate_slot==0||maximum_cleanup>OC_GAME_GC_MAX_DELETES)
        return OC_GAME_STORAGE_INVALID;
    result=recover_without_cleanup(ops,game_id,&current);
    if(result!=OC_GAME_STORAGE_OK)return result;
    if(current.tombstone)next=current;
    else {
        result=oc_game_selection_next(&current,current.game_id,current.version,
                                      current.bundle_id,current.inventory_digest,
                                      0u,1,&next);
        if(result!=OC_GAME_STORAGE_OK)return result;
        result=commit_selector(ops,game_id,&next);
        if(result!=OC_GAME_STORAGE_OK)return result;
    }
    *committed=next;
    if(maximum_cleanup==0u){if(cleanup_deletes!=0)*cleanup_deletes=0u;return OC_GAME_STORAGE_OK;}
    return cleanup_slots(ops,game_id,&next,maximum_cleanup,cleanup_deletes);
}
