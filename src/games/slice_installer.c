#include "openconsole/net_protocol.h"

#define BUNDLE_CAPACITY (2u * 1024u * 1024u)
#define EFI_LOCATE_BY_PROTOCOL 2u
#define EFI_FILE_DIRECTORY 0x10u

static uint8_t bundle[BUNDLE_CAPACITY];
static const EFI_GUID simple_fs_guid =
    {0x964e5b22u,0x6459u,0x11d2u,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

static void debug_write(const char *text) {
    while (*text) __asm__ volatile("outb %0, %1"::"a"((uint8_t)*text++),"Nd"((uint16_t)0x402));
}
static void debug_hex(uint64_t value) {
    static const char digits[]="0123456789ABCDEF"; int shift;
    for(shift=60;shift>=0;shift-=4)
        __asm__ volatile("outb %0, %1"::"a"((uint8_t)digits[(value>>(uint32_t)shift)&15u]),"Nd"((uint16_t)0x402));
    debug_write("\n");
}
static void zero(void *memory, UINTN size) {
    uint8_t *p=memory; while(size--)*p++=0u;
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static int same(const uint8_t *a,const char *b,UINTN n) {
    while(n--)if(*a++!=(uint8_t)*b++)return 0; return 1;
}
static EFI_STATUS await(OC_NET_PROTOCOL *net,OC_NET_SESSION *session,
                        OC_NET_OPERATION *operation,EFI_BOOT_SERVICES *bs,
                        uint64_t *now,OC_NET_OPERATION_STATUS *result) {
    for(;;){EFI_STATUS status;
        result->structure_size=sizeof(*result);
        status=net->query_operation(operation,result);
        if(EFI_ERROR(status)||result->result!=OC_NET_RESULT_PENDING)return status;
        *now+=1000u; status=net->pump(session,*now);
        if(EFI_ERROR(status))return status; (void)bs->stall(1000u);
    }
}
static EFI_STATUS write_file(EFI_FILE_PROTOCOL *dir,const CHAR16 *name,
                             const void *bytes,UINTN size) {
    EFI_FILE_PROTOCOL *file=0; UINTN written=size;
    EFI_STATUS status=dir->open(dir,&file,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|
                                EFI_FILE_MODE_CREATE,0u);
    if(EFI_ERROR(status))return status;
    status=file->set_position(file,0u);
    if(!EFI_ERROR(status))status=file->write(file,&written,bytes);
    if(!EFI_ERROR(status)&&written!=size)status=EFI_DEVICE_ERROR;
    if(!EFI_ERROR(status))status=file->flush(file);
    (void)file->close(file); return status;
}
static void message(EFI_SYSTEM_TABLE *st,const CHAR16 *text) {
    if(st->con_out&&st->con_out->output_string)(void)st->con_out->output_string(st->con_out,text);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *st) {
    static const char url[]="https://10.0.2.2:9443/slice.ocg";
    static const CHAR16 games_name[]=L"games", game_name[]=L"integrated";
    static const CHAR16 manifest_name[]=L"game.toml", controls_name[]=L"controls.cfg";
    static const CHAR16 jacket_name[]=L"jacket.ocj", efi_name[]=L"game.efi";
    EFI_BOOT_SERVICES *bs=st->boot_services; OC_NET_PROTOCOL *net=0;
    OC_NET_SESSION *session=0; OC_NET_OPERATION *operation=0;
    OC_NET_OPERATION_STATUS result; OC_NET_SESSION_DESC session_desc={sizeof(session_desc),OC_NET_ABI_VERSION,BUNDLE_CAPACITY*2u,8u,0u};
    OC_NET_DEADLINE deadline={sizeof(deadline),0u,15000000u};
    OC_NET_HTTPS_DESC request;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=0; EFI_FILE_PROTOCOL *root=0,*games=0,*game=0;
    uint64_t now=0u,total; uint32_t lengths[4]; const uint8_t *part; UINTN i;
    EFI_STATUS status;
    (void)image;
    message(st,L"\r\nOpenConsole Slice Installer\r\nConnecting and downloading verified package...\r\n");
    debug_write("OPENCONSOLE_SLICE_DOWNLOAD_BEGIN\n");
    status=bs->locate_protocol(&OC_NET_PROTOCOL_GUID,0,(void**)&net);
    if(EFI_ERROR(status)||net==0)return EFI_NOT_FOUND;
    status=net->create_session(&session_desc,&session); if(EFI_ERROR(status))goto done;
    status=net->configure_dhcp4(session,&deadline,&operation);
    if(EFI_ERROR(status)){debug_write("OPENCONSOLE_SLICE_DHCP_START_STATUS_");debug_hex(status);goto done;}
    status=await(net,session,operation,bs,&now,&result);
    if(EFI_ERROR(status)){debug_write("OPENCONSOLE_SLICE_DHCP_AWAIT_STATUS_");debug_hex(status);goto done;}
    if(result.result!=OC_NET_RESULT_OK){
        debug_write("OPENCONSOLE_SLICE_DHCP_RESULT_");debug_hex(result.result);
        debug_write("OPENCONSOLE_SLICE_DHCP_PROTOCOL_STATUS_");debug_hex(result.protocol_status);
        status=EFI_DEVICE_ERROR;goto done;}
    (void)net->release_operation(operation); operation=0;
    debug_write("OPENCONSOLE_SLICE_DHCP_OK\n");
    zero(&request,sizeof(request)); request.structure_size=sizeof(request);
    request.url=url; request.url_size=sizeof(url)-1u; request.response_body=bundle;
    request.response_capacity=sizeof(bundle); request.deadline.structure_size=sizeof(request.deadline);
    request.deadline.deadline_usec=now+180000000u;
    for(i=0u;i<100u;++i){status=net->start_https(session,&request,&operation);
        if(status!=EFI_NO_MAPPING)break; now+=100000u;(void)net->pump(session,now);(void)bs->stall(100000u);}
    if(EFI_ERROR(status)){debug_write("OPENCONSOLE_SLICE_HTTPS_START_STATUS_");debug_hex(status);goto done;}
    status=await(net,session,operation,bs,&now,&result);
    if(EFI_ERROR(status)){debug_write("OPENCONSOLE_SLICE_HTTPS_AWAIT_STATUS_");debug_hex(status);goto done;}
    if(result.result!=OC_NET_RESULT_OK){
        debug_write("OPENCONSOLE_SLICE_HTTPS_RESULT_");debug_hex(result.result);
        debug_write("OPENCONSOLE_SLICE_HTTPS_PROTOCOL_STATUS_");debug_hex(result.protocol_status);
        status=EFI_DEVICE_ERROR;goto done;}
    total=result.bytes_transferred; (void)net->release_operation(operation);operation=0;
    debug_write("OPENCONSOLE_SLICE_HTTPS_OK\n");
    if(total<20u||!same(bundle,"OCG1",4u)){status=EFI_SECURITY_VIOLATION;goto done;}
    for(i=0u;i<4u;++i)lengths[i]=le32(bundle+4u+i*4u);
    if((uint64_t)20u+lengths[0]+lengths[1]+lengths[2]+lengths[3]!=total||
       lengths[0]==0u||lengths[3]<2u){status=EFI_SECURITY_VIOLATION;goto done;}
    part=bundle+20u+lengths[0]+lengths[1]+lengths[2];
    if(part[0]!='M'||part[1]!='Z'){status=EFI_SECURITY_VIOLATION;goto done;}
    debug_write("OPENCONSOLE_SLICE_BUNDLE_OK\n");
    status=bs->locate_protocol(&simple_fs_guid,0,(void**)&fs);
    if(EFI_ERROR(status)||EFI_ERROR(fs->open_volume(fs,&root))){status=EFI_NOT_FOUND;goto done;}
    debug_write("OPENCONSOLE_SLICE_FS_ROOT_OK\n");
    status=root->open(root,&games,games_name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0u);
    if(EFI_ERROR(status))goto done;
    debug_write("OPENCONSOLE_SLICE_FS_GAMES_OK\n");
    status=games->open(games,&game,game_name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|
                       EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
    if(EFI_ERROR(status))goto done;
    debug_write("OPENCONSOLE_SLICE_FS_GAME_DIR_OK\n");
    part=bundle+20u;
    if(EFI_ERROR(status=write_file(game,manifest_name,part,lengths[0])))goto done; part+=lengths[0];
    if(EFI_ERROR(status=write_file(game,controls_name,part,lengths[1])))goto done; part+=lengths[1];
    if(EFI_ERROR(status=write_file(game,jacket_name,part,lengths[2])))goto done; part+=lengths[2];
    if(EFI_ERROR(status=write_file(game,efi_name,part,lengths[3])))goto done;
    status=game->flush(game);
    if(!EFI_ERROR(status)){
        debug_write("OPENCONSOLE_SLICE_INSTALL_COMPLETE\n");
        message(st,L"Download complete. Package installed to the boot device.\r\nPress Enter to return to the library.\r\n");
        {EFI_INPUT_KEY key; while(EFI_ERROR(st->con_in->read_key_stroke(st->con_in,&key))||key.unicode_char!=13u)
            (void)bs->wait_for_event(1u,&st->con_in->wait_for_key,&i);}
    }
done:
    if(operation)(void)net->release_operation(operation);
    if(session)(void)net->destroy_session(session);
    if(game)(void)game->close(game); if(games)(void)games->close(games); if(root)(void)root->close(root);
    if(EFI_ERROR(status)){debug_write("OPENCONSOLE_SLICE_INSTALL_FAILED_STATUS_");debug_hex(status);}
    return status;
}
