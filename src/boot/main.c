#include "openconsole/manifest.h"
#include "openconsole/platform_debug.h"
#include "openconsole/capabilities.h"
#include "openconsole/gpu_probe.h"
#include "openconsole/io_inventory.h"
#include "openconsole/net_provider.h"
#include "openconsole/net_stack_loader.h"
#include "openconsole/net_snp_bridge.h"
#include "openconsole/ethernet_catalog.h"
#include "openconsole/ethernet_driver_selector.h"
#include "openconsole/uefi_net_backend.h"
#include "openconsole/audioio_backend.h"
#include "openconsole/crypto_protocol.h"
#include "openconsole/tls_ca.h"
#if OPENCONSOLE_QEMU_TEST && !OPENCONSOLE_REAL_NET_TEST && !OPENCONSOLE_REAL_AUDIO_TEST && !OPENCONSOLE_INTEGRATED_IO_TEST
#include "openconsole/io_mock_provider.h"
#endif
#include "openconsole/gles_provider.h"
#include "openconsole/uefi.h"
#include "openconsole/dashboard.h"
#include "openconsole/version.h"
#include "openconsole/time_format.h"
#include "openconsole/package_measure.h"
#include "openconsole/details_input.h"
#include "openconsole/delete_policy.h"
#include "openconsole/sha256.h"
#include "openconsole/pe_image.h"
#include "openconsole/platform_inventory.h"
#include "openconsole/publisher_keys.h"
#include "openconsole/game_storage.h"
#include "openconsole/uefi_game_storage.h"
#include "openconsole/launch_recovery.h"
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
#include "openconsole_platform_trust.h"
#endif

#define MAX_GAMES OC_DASHBOARD_MAX_GAMES
#define MAX_FILE_BYTES (16u * 1024u * 1024u)
#define EFI_GRAPHICS_OUTPUT_BLT_VIDEO_FILL 0u
#define EFI_GRAPHICS_OUTPUT_BLT_BUFFER_TO_VIDEO 2u
#define EFI_LOCATE_BY_PROTOCOL 2u
#define EFI_EVENT_TIMER 0x80000000u
#define EFI_TIMER_PERIODIC 1u
#define EFI_TPL_APPLICATION 4u
typedef EFI_STATUS(EFIAPI *oc_set_watchdog_timer)(UINTN,uint64_t,UINTN,const CHAR16*);

static const EFI_GUID loaded_image_guid =
    {0x5b1b31a1u, 0x9562u, 0x11d2u, {0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const EFI_GUID simple_fs_guid =
    {0x964e5b22u, 0x6459u, 0x11d2u, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
static const EFI_GUID device_path_guid =
    {0x09576e91u, 0x6d3fu, 0x11d2u, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
#endif
static const EFI_GUID file_info_guid =
    {0x09576e92u, 0x6d3fu, 0x11d2u, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const EFI_GUID gop_guid =
    {0x9042a9deu, 0x23dcu, 0x4a38u, {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
static const EFI_GUID simple_pointer_guid =
    {0x31878c87u, 0x0b75u, 0x11d5u, {0x9a,0x4f,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
static const EFI_GUID usb_io_guid =
    {0x2b2f68d6u, 0x0cd2u, 0x44cfu, {0x8e,0x8b,0xbb,0xa2,0x0b,0x1b,0x5b,0x75}};
#if !OPENCONSOLE_QEMU_TEST || OPENCONSOLE_REAL_NET_TEST || OPENCONSOLE_REAL_AUDIO_TEST || OPENCONSOLE_INTEGRATED_IO_TEST
static const EFI_GUID pci_io_guid =
    {0x4cf5b200u, 0x68b8u, 0x4ca5u, {0x9e,0xec,0xb2,0x3e,0x3f,0x50,0x02,0x9a}};
#endif
#if !OPENCONSOLE_QEMU_TEST || OPENCONSOLE_REAL_NET_TEST || OPENCONSOLE_INTEGRATED_IO_TEST
static const EFI_GUID simple_network_guid =
    {0xa19832b9u,0xac25u,0x11d3u,{0x9a,0x2d,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
static const EFI_GUID nii31_guid =
    {0x1aced566u,0x76edu,0x4218u,{0xbc,0x81,0x76,0x7f,0x1f,0x97,0x7a,0x89}};
#endif

typedef struct {
    CHAR16 directory[129];
    oc_game_manifest manifest;
    void *jacket;
    UINTN jacket_size;
    uint64_t package_size;
    uint64_t manifest_hash;
    uint8_t host_supported;
    uint8_t hardware_accel_supported;
    uint8_t transactional;
    uint8_t quarantined;
    oc_game_selection selection;
} game_entry;

typedef struct {
    EFI_HANDLE image;
    EFI_SYSTEM_TABLE *st;
    EFI_BOOT_SERVICES *bs;
    EFI_FILE_PROTOCOL *root;
    EFI_FILE_PROTOCOL *boot_root;
    uint8_t data_volume_ambiguous;
    EFI_FILE_PROTOCOL *inventory_log;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_SIMPLE_POINTER_PROTOCOL *pointer;
    EFI_EVENT pointer_watchdog;
    game_entry games[MAX_GAMES];
    UINTN game_count;
    oc_dashboard_view dashboard;
    const char *status;
    OC_GLES_PROVIDER gles_provider;
    EFI_HANDLE gles_handle;
#if OPENCONSOLE_QEMU_TEST && !OPENCONSOLE_REAL_NET_TEST && !OPENCONSOLE_REAL_AUDIO_TEST && !OPENCONSOLE_INTEGRATED_IO_TEST
    oc_io_mock_provider io_mock;
    EFI_HANDLE net_handle;
    EFI_HANDLE audio_handle;
    uint64_t child_generation;
#endif
    oc_uefi_net_backend *net_backend;
    oc_net_provider *net_provider;
    EFI_HANDLE net_provider_handle;
    uint64_t net_generation;
    UINTN net_allocation_bytes;
    UINTN net_peak_allocation_bytes;
    UINTN net_allocation_count;
    EFI_HANDLE net_stack_drivers[OC_NET_DRIVER_HANDLE_CAPACITY];
    UINTN net_stack_driver_count;
    uint8_t net_ready;
    uint8_t net_online;
    oc_audioio_backend *audio_backend;
    oc_audio_provider *audio_provider;
    EFI_HANDLE audio_provider_handle;
    EFI_HANDLE audio_driver_image;
    EFI_HANDLE crypto_driver_image;
    OC_CRYPTO_PROTOCOL *crypto_protocol;
    void *platform_inventory_blob;
    UINTN platform_inventory_blob_size;
    oc_platform_inventory platform_inventory;
    UINTN crypto_driver_size;
    uint8_t platform_inventory_ready;
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    void *ethernet_catalog_blob;
    oc_eth_catalog ethernet_catalog;
    oc_eth_catalog_entry ethernet_entries[OC_ETH_CATALOG_MAX_ENTRIES];
    oc_eth_candidate ethernet_candidates[OC_ETH_CATALOG_MAX_ENTRIES];
    char ethernet_ids[OC_ETH_CATALOG_MAX_ENTRIES][OC_ETH_CATALOG_ID_BYTES];
    UINTN ethernet_candidate_count;
#endif
    void *publisher_keys_blob;
    oc_publisher_key_store publisher_keys;
    uint8_t publisher_keys_ready;
    oc_uefi_game_storage game_storage;
    uint8_t game_storage_ready;
    oc_launch_record launch_record;
    uint8_t launch_record_ready;
    uint8_t safe_mode;
    EFI_HANDLE input_driver_image;
    uint64_t audio_generation;
    uint8_t audio_ready;
#if OPENCONSOLE_QEMU_TEST
    uint32_t successful_child_returns;
    uint32_t pointer_wait_events;
#endif
    UINTN modal_game;
    uint8_t modal_open;
    uint8_t modal_delete_confirm;
    uint8_t modal_pressed;
    uint8_t modal_launch_requested;
    uint8_t modal_delete_requested;
} launcher;

static void debug_write(const char *text);
static void debug_putc(char value);
static void debug_hex_uintn(UINTN value);
static void zero_bytes(void *destination, UINTN size);
static EFI_STATUS read_file(launcher *, EFI_FILE_PROTOCOL *, const CHAR16 *,
                            UINTN, void **, UINTN *);
static int directory_matches_id(const CHAR16 *, const char *);

static uint64_t hash_bytes(const void *data, UINTN size) {
    const uint8_t *bytes = data;
    uint64_t hash = 1469598103934665603ull;
    while (size-- != 0u) {
        hash ^= *bytes++;
        hash *= 1099511628211ull;
    }
    return hash;
}

static void copy_bytes(void *destination, const void *source, UINTN size) {
    uint8_t *out = destination;
    const uint8_t *in = source;
    while (size-- != 0u) *out++ = *in++;
}

static EFI_STATUS ensure_update_crypto(launcher *app) {
    static const CHAR16 driver_path[] =
        L"\\EFI\\OpenConsole\\Drivers\\Crypto\\OpenConsoleCryptoDxe.efi";
    OC_CRYPTO_PROTOCOL *protocol = 0;
    void *image_data = 0;
    UINTN image_size = 0u;
    EFI_STATUS status = app->bs->locate_protocol(
        &OC_CRYPTO_PROTOCOL_GUID, 0, (void **)&protocol);
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    if (!EFI_ERROR(status)) {
        debug_write("OPENCONSOLE_INTEGRITY_UNEXPECTED_CRYPTO_PROVIDER\n");
        return EFI_SECURITY_VIOLATION;
    }
#else
    if (!EFI_ERROR(status)) { app->crypto_protocol = protocol; return EFI_SUCCESS; }
#endif
    status = read_file(app, app->boot_root, driver_path, 2u * 1024u * 1024u,
                       &image_data, &image_size);
    if (EFI_ERROR(status)) {
        debug_write("OPENCONSOLE_CRYPTO_OFFLINE_NO_DRIVER\n");
        return status;
    }
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    {
        uint8_t digest[32];
        oc_pe_info pe;
        UINTN i;
        if (!oc_sha256(image_data, image_size, digest)) status = EFI_SECURITY_VIOLATION;
        else {
            for (i = 0u; i < sizeof(digest); ++i)
                if (digest[i] != oc_pinned_crypto_driver_sha256[i]) break;
            if (i != sizeof(digest) ||
                oc_pe_validate_native_efi(image_data, image_size, 2u * 1024u * 1024u,
                                       OC_PE_ROLE_DRIVER, &pe) != OC_PE_OK)
                status = EFI_SECURITY_VIOLATION;
        }
        if (EFI_ERROR(status)) {
            debug_write("OPENCONSOLE_INTEGRITY_CRYPTO_BOOTSTRAP_REJECTED\n");
            app->bs->free_pool(image_data);
            return status;
        }
        debug_write("OPENCONSOLE_INTEGRITY_CRYPTO_BOOTSTRAP_VERIFIED\n");
    }
#endif
    debug_write("OPENCONSOLE_CRYPTO_DRIVER_LOADING\n");
    app->crypto_driver_size = image_size;
    status = app->bs->load_image(0u, app->image, 0, image_data, image_size,
                                 &app->crypto_driver_image);
    (void)app->bs->free_pool(image_data);
    if (EFI_ERROR(status)) return status;
    status = app->bs->start_image(app->crypto_driver_image, 0, 0);
    if (EFI_ERROR(status)) {
        (void)app->bs->unload_image(app->crypto_driver_image);
        app->crypto_driver_image = 0;
        return status;
    }
    status = app->bs->locate_protocol(&OC_CRYPTO_PROTOCOL_GUID, 0,
                                      (void **)&protocol);
    if (EFI_ERROR(status) || protocol == 0 ||
        protocol->revision != OC_CRYPTO_PROTOCOL_REVISION ||
        protocol->structure_size < sizeof(*protocol) || protocol->sha256 == 0 ||
        protocol->ed448_verify == 0)
        return EFI_INCOMPATIBLE_VERSION;
    app->crypto_protocol = protocol;
    debug_write("OPENCONSOLE_CRYPTO_PROVIDER_READY\n");
    return EFI_SUCCESS;
}

#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
static int platform_signature_verify(void *context,const uint8_t key[57],
                                     const uint8_t *message,size_t message_size,
                                     const uint8_t signature[114]) {
    launcher *app=context;
    return app!=0&&app->crypto_protocol!=0&&
        !EFI_ERROR(app->crypto_protocol->ed448_verify(app->crypto_protocol,key,
            message,(UINTN)message_size,signature));
}

static EFI_STATUS load_platform_inventory(launcher *app) {
    static const CHAR16 path[]=L"\\EFI\\OpenConsole\\platform.ocp";
    static const char crypto_path[]="EFI/OpenConsole/Drivers/Crypto/OpenConsoleCryptoDxe.efi";
    void *blob=0,*workspace=0;UINTN blob_size=0u,i;EFI_STATUS status;
    oc_platform_result result;oc_platform_record crypto;
    status=read_file(app,app->root,path,128u+114u+OC_PLATFORM_MAX_INDEX,&blob,&blob_size);
    if(EFI_ERROR(status)){debug_write("OPENCONSOLE_INTEGRITY_PLATFORM_INVENTORY_MISSING\n");return status;}
    result=oc_platform_inventory_parse(blob,blob_size,&app->platform_inventory);
    if(result!=OC_PLATFORM_OK){debug_write("OPENCONSOLE_INTEGRITY_PLATFORM_INVENTORY_PARSE_REJECTED\n");app->bs->free_pool(blob);return EFI_SECURITY_VIOLATION;}
    status=app->bs->allocate_pool(EFI_LOADER_DATA,OC_PLATFORM_SIGNATURE_WORKSPACE,&workspace);
    if(EFI_ERROR(status)){app->bs->free_pool(blob);return status;}
    result=oc_platform_inventory_verify(&app->platform_inventory,
        oc_pinned_platform_public_key,platform_signature_verify,app,workspace,
        OC_PLATFORM_SIGNATURE_WORKSPACE);
    app->bs->free_pool(workspace);
    if(result!=OC_PLATFORM_OK){debug_write("OPENCONSOLE_INTEGRITY_PLATFORM_SIGNATURE_REJECTED\n");app->bs->free_pool(blob);return EFI_SECURITY_VIOLATION;}
    result=oc_platform_inventory_find(&app->platform_inventory,crypto_path,
        OC_PLATFORM_ROLE_CRYPTO,&crypto);
    if(result!=OC_PLATFORM_OK||crypto.file_size!=app->crypto_driver_size){app->bs->free_pool(blob);debug_write("OPENCONSOLE_INTEGRITY_CRYPTO_NOT_AUTHORIZED\n");return EFI_SECURITY_VIOLATION;}
    for(i=0u;i<32u;i++)if(crypto.digest[i]!=oc_pinned_crypto_driver_sha256[i])break;
    if(i!=32u){app->bs->free_pool(blob);debug_write("OPENCONSOLE_INTEGRITY_CRYPTO_NOT_AUTHORIZED\n");return EFI_SECURITY_VIOLATION;}
    app->platform_inventory_blob=blob;app->platform_inventory_blob_size=blob_size;
    app->platform_inventory_ready=1u;
    debug_write("OPENCONSOLE_INTEGRITY_PLATFORM_INVENTORY_VERIFIED\n");
    debug_write("OPENCONSOLE_INTEGRITY_CRYPTO_INVENTORY_AUTHORIZED\n");
    return EFI_SUCCESS;
}

static int canonical_ascii_path(const CHAR16 *path,char out[OC_PLATFORM_MAX_PATH+1u]) {
    UINTN source=0u,used=0u;if(path==0)return 0;if(path[0]=='\\')source=1u;
    while(path[source]!=0u){CHAR16 c=path[source++];if(c>=0x80u||used>=OC_PLATFORM_MAX_PATH)return 0;out[used++]=(char)(c=='\\'?'/':c);}
    out[used]='\0';return used!=0u;
}

static EFI_STATUS read_authorized_platform_image(launcher *app,const CHAR16 *path,
    uint16_t role,UINTN maximum,void **data,UINTN *size) {
    char canonical[OC_PLATFORM_MAX_PATH+1u];oc_platform_record record;oc_pe_info pe;
    oc_platform_result result;EFI_STATUS status;
    if(!app->platform_inventory_ready||!canonical_ascii_path(path,canonical))return EFI_SECURITY_VIOLATION;
    status=read_file(app,app->boot_root,path,maximum,data,size);if(EFI_ERROR(status))return status;
    result=oc_platform_inventory_find(&app->platform_inventory,canonical,role,&record);
    if(result==OC_PLATFORM_OK)result=oc_platform_record_verify_bytes(&record,*data,*size);
    if(result==OC_PLATFORM_OK&&oc_pe_validate_native_efi(*data,*size,maximum,OC_PE_ROLE_DRIVER,&pe)!=OC_PE_OK)result=OC_PLATFORM_FILE_DIGEST;
    if(result!=OC_PLATFORM_OK){app->bs->free_pool(*data);*data=0;*size=0;debug_write("OPENCONSOLE_INTEGRITY_PLATFORM_IMAGE_REJECTED\n");return EFI_SECURITY_VIOLATION;}
    debug_write("OPENCONSOLE_INTEGRITY_PLATFORM_IMAGE_AUTHORIZED\n");return EFI_SUCCESS;
}

static EFI_STATUS read_authorized_platform_blob(launcher *app,const CHAR16 *path,
    uint16_t role,UINTN maximum,void **data,UINTN *size) {
    char canonical[OC_PLATFORM_MAX_PATH+1u];oc_platform_record record;
    oc_platform_result result;EFI_STATUS status;
    if(!app->platform_inventory_ready||!canonical_ascii_path(path,canonical))return EFI_SECURITY_VIOLATION;
    status=read_file(app,app->boot_root,path,maximum,data,size);if(EFI_ERROR(status))return status;
    result=oc_platform_inventory_find(&app->platform_inventory,canonical,role,&record);
    if(result==OC_PLATFORM_OK)result=oc_platform_record_verify_bytes(&record,*data,*size);
    if(result!=OC_PLATFORM_OK){app->bs->free_pool(*data);*data=0;*size=0;return EFI_SECURITY_VIOLATION;}
    return EFI_SUCCESS;
}

static EFI_STATUS load_ethernet_catalog(launcher *app) {
    static const CHAR16 path[]=L"\\EFI\\OpenConsole\\Drivers\\Network\\catalog.oec";
    void *blob=0;UINTN size=0u,index;EFI_STATUS status;oc_eth_catalog_result parsed;
    status=read_authorized_platform_blob(app,path,OC_PLATFORM_ROLE_NETWORK,
        32u+OC_ETH_CATALOG_MAX_ENTRIES*192u,&blob,&size);
    if(EFI_ERROR(status)){debug_write("OPENCONSOLE_ETH_CATALOG_NOT_AVAILABLE\n");return status;}
    parsed=oc_eth_catalog_parse(blob,size,&app->ethernet_catalog);
    if(parsed!=OC_ETH_CATALOG_OK){app->bs->free_pool(blob);debug_write("OPENCONSOLE_ETH_CATALOG_REJECTED\n");return EFI_SECURITY_VIOLATION;}
    app->ethernet_catalog_blob=blob;
    app->ethernet_candidate_count=app->ethernet_catalog.entry_count;
    for(index=0u;index<app->ethernet_candidate_count;index++){
        oc_eth_catalog_entry *entry=&app->ethernet_entries[index];
        oc_eth_candidate *candidate=&app->ethernet_candidates[index];UINTN text;
        if(oc_eth_catalog_entry_at(&app->ethernet_catalog,(uint32_t)index,entry)!=OC_ETH_CATALOG_OK)return EFI_SECURITY_VIOLATION;
        for(text=0u;text<entry->manifest_id_size;text++)app->ethernet_ids[index][text]=(char)entry->manifest_id[text];
        app->ethernet_ids[index][entry->manifest_id_size]='\0';
        candidate->manifest_id=app->ethernet_ids[index];candidate->artifact=entry;
        candidate->artifact_size=(UINTN)entry->artifact_size;
        candidate->architecture=(oc_eth_architecture)entry->architecture;
        candidate->vendor=entry->vendor;candidate->device=entry->device;
        candidate->subsystem_vendor=entry->subsystem_vendor;
        candidate->subsystem_device=entry->subsystem_device;
        candidate->revision_min=entry->revision_min;candidate->revision_max=entry->revision_max;
        candidate->expected_protocols=entry->protocols;candidate->priority=entry->priority;
    }
    debug_write("OPENCONSOLE_ETH_CATALOG_AUTHENTICATED\n");return EFI_SUCCESS;
}

static EFI_STATUS load_publisher_keys(launcher *app) {
    static const CHAR16 path[]=L"\\EFI\\OpenConsole\\Trust\\PublisherKeys.ocpk";
    static const char canonical[]="EFI/OpenConsole/Trust/PublisherKeys.ocpk";
    void *blob=0;UINTN size=0;oc_platform_record record;oc_platform_result platform;
    oc_publisher_keys_result parsed;EFI_STATUS status;
    status=read_file(app,app->boot_root,path,128u*1024u,&blob,&size);if(EFI_ERROR(status))return status;
    platform=oc_platform_inventory_find(&app->platform_inventory,canonical,OC_PLATFORM_ROLE_TRUST,&record);
    if(platform==OC_PLATFORM_OK)platform=oc_platform_record_verify_bytes(&record,blob,size);
    if(platform!=OC_PLATFORM_OK){app->bs->free_pool(blob);debug_write("OPENCONSOLE_INTEGRITY_PUBLISHER_KEYS_REJECTED\n");return EFI_SECURITY_VIOLATION;}
    parsed=oc_publisher_keys_parse(blob,size,&app->publisher_keys);
    if(parsed!=OC_PUBLISHER_KEYS_OK){app->bs->free_pool(blob);debug_write("OPENCONSOLE_INTEGRITY_PUBLISHER_KEYS_REJECTED\n");return EFI_SECURITY_VIOLATION;}
    app->publisher_keys_blob=blob;app->publisher_keys_ready=1u;
    debug_write("OPENCONSOLE_INTEGRITY_PUBLISHER_KEYS_VERIFIED\n");return EFI_SUCCESS;
}

static EFI_STATUS write_launch_record(launcher *app,const oc_launch_record *record) {
    static const CHAR16 path_a[]=L"\\EFI\\OpenConsole\\State\\Launch\\attempt-a.oclr";
    static const CHAR16 path_b[]=L"\\EFI\\OpenConsole\\State\\Launch\\attempt-b.oclr";
    const CHAR16 *path=record->slot?path_b:path_a;uint8_t bytes[OC_LAUNCH_RECORD_BYTES];
    EFI_FILE_PROTOCOL *old=0,*file=0;EFI_STATUS status;UINTN size=sizeof(bytes);
    if(!oc_launch_record_encode(record,bytes))return EFI_INVALID_PARAMETER;
    status=app->root->open(app->root,&old,path,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0u);
    if(!EFI_ERROR(status)){status=old->delete_file(old);old=0;if(EFI_ERROR(status))return status;}
    else if(status!=EFI_NOT_FOUND)return status;
    status=app->root->open(app->root,&file,path,
        EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0u);
    if(EFI_ERROR(status)||!file)return status;
    status=file->write(file,&size,bytes);
    if(!EFI_ERROR(status)&&size==sizeof(bytes))status=file->flush(file);
    if(EFI_ERROR(file->close(file))&&!EFI_ERROR(status))status=EFI_DEVICE_ERROR;
    if(EFI_ERROR(status)||size!=sizeof(bytes))return EFI_DEVICE_ERROR;
    app->launch_record=*record;app->launch_record_ready=1u;return EFI_SUCCESS;
}

static EFI_STATUS load_launch_recovery(launcher *app) {
    static const CHAR16 path_a[]=L"\\EFI\\OpenConsole\\State\\Launch\\attempt-a.oclr";
    static const CHAR16 path_b[]=L"\\EFI\\OpenConsole\\State\\Launch\\attempt-b.oclr";
    void *a=0,*b=0;UINTN an=0,bn=0;oc_launch_record record,next;EFI_STATUS status=EFI_SUCCESS;
    (void)read_file(app,app->root,path_a,OC_LAUNCH_RECORD_BYTES,&a,&an);
    (void)read_file(app,app->root,path_b,OC_LAUNCH_RECORD_BYTES,&b,&bn);
    if(oc_launch_record_recover(a,an,b,bn,&record)) {
        app->launch_record=record;app->launch_record_ready=1u;
        if(record.state==OC_LAUNCH_ATTEMPT) {
            if(!oc_launch_record_next(&record,record.game_id,record.version,
                    record.bundle_id,OC_LAUNCH_QUARANTINE,&next))status=EFI_SECURITY_VIOLATION;
            else status=write_launch_record(app,&next);
            if(!EFI_ERROR(status)){app->safe_mode=1u;debug_write("OPENCONSOLE_RECOVERY_GAME_QUARANTINED\n");}
        } else if(record.state==OC_LAUNCH_QUARANTINE) app->safe_mode=1u;
    }
    if(a)app->bs->free_pool(a);if(b)app->bs->free_pool(b);return status;
}

static EFI_STATUS record_launch_state(launcher *app,const game_entry *game,
                                      oc_launch_state state) {
    oc_launch_record next;
    if(!oc_launch_record_next(app->launch_record_ready?&app->launch_record:0,
        game->manifest.id,game->manifest.version,game->selection.bundle_id,state,&next))
        return EFI_SECURITY_VIOLATION;
    return write_launch_record(app,&next);
}

static EFI_STATUS clear_launch_quarantine(launcher *app) {
    oc_launch_record next;
    if(!app->launch_record_ready||app->launch_record.state!=OC_LAUNCH_QUARANTINE||
       !oc_launch_record_next(&app->launch_record,app->launch_record.game_id,
        app->launch_record.version,app->launch_record.bundle_id,OC_LAUNCH_COMPLETE,&next))
        return EFI_NOT_FOUND;
    return write_launch_record(app,&next);
}
#endif

static void install_fixture_tls_ca(launcher *app) {
    static const CHAR16 path[] = L"\\EFI\\OpenConsole\\Trust\\ca.der";
    void *der = 0;
    UINTN der_size = 0u;
    EFI_STATUS status = read_file(app, app->boot_root, path, OC_TLS_CA_MAX_DER_BYTES,
                                  &der, &der_size);
    if (EFI_ERROR(status)) return;
    status = oc_tls_ca_install(app->bs, app->st->runtime_services, der, der_size);
    (void)app->bs->free_pool(der);
    if (EFI_ERROR(status)) debug_write("OPENCONSOLE_TLS_CA_INSTALL_FAILED\n");
    else debug_write("OPENCONSOLE_TLS_CA_INSTALLED\n");
}

static EFI_STATUS ensure_simple_pointer_driver(launcher *app) {
    static const CHAR16 driver_path[] =
        L"\\EFI\\OpenConsole\\Drivers\\Input\\UsbMouseDxe.efi";
    EFI_HANDLE *handles = 0;
    UINTN handle_count = 0u, index;
    void *image_data = 0;
    UINTN image_size = 0u;
    EFI_STATUS status;
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    status = read_authorized_platform_image(app,driver_path,OC_PLATFORM_ROLE_INPUT,
        2u*1024u*1024u,&image_data,&image_size);
#else
    status = read_file(app, app->boot_root, driver_path, 2u * 1024u * 1024u,
                       &image_data, &image_size);
#endif
    if (EFI_ERROR(status)) {
        debug_write("OPENCONSOLE_INPUT_DRIVER_MISSING\n");
        return status;
    }
    status = app->bs->load_image(0u, app->image, 0, image_data, image_size,
                                 &app->input_driver_image);
    (void)app->bs->free_pool(image_data);
    if (EFI_ERROR(status)) return status;
    status = app->bs->start_image(app->input_driver_image, 0, 0);
    if (EFI_ERROR(status)) return status;
    debug_write("OPENCONSOLE_INPUT_DRIVER_STARTED\n");
    status = app->bs->locate_handle_buffer(EFI_LOCATE_BY_PROTOCOL,
        &usb_io_guid, 0, &handle_count, &handles);
    if (!EFI_ERROR(status)) {
        for (index = 0u; index < handle_count; ++index)
            (void)app->bs->connect_controller(handles[index],
                app->input_driver_image, 0, 1u);
        (void)app->bs->free_pool(handles);
    }
    return EFI_SUCCESS;
}

#if !OPENCONSOLE_QEMU_TEST || OPENCONSOLE_REAL_NET_TEST || OPENCONSOLE_REAL_AUDIO_TEST || OPENCONSOLE_INTEGRATED_IO_TEST
static EFI_STATUS ensure_audioio(launcher *app) {
    static const CHAR16 driver_path[] =
        L"\\EFI\\OpenConsole\\Drivers\\Audio\\AudioDxe.efi";
    EFI_HANDLE *handles = 0;
    UINTN handle_count = 0u;
    UINTN index;
    void *protocol = 0;
    void *image_data = 0;
    UINTN image_size = 0u;
    EFI_STATUS status = app->bs->locate_protocol(
        &OC_AUDIO_IO_PROTOCOL_GUID, 0, &protocol);
    if (!EFI_ERROR(status)) return EFI_SUCCESS;
    debug_write("OPENCONSOLE_AUDIO_DRIVER_READING\n");
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    status = read_authorized_platform_image(app,driver_path,OC_PLATFORM_ROLE_AUDIO,
        4u*1024u*1024u,&image_data,&image_size);
#else
    status = read_file(app, app->boot_root, driver_path, 4u * 1024u * 1024u,
                       &image_data, &image_size);
#endif
    if (EFI_ERROR(status)) return status;
    debug_write("OPENCONSOLE_AUDIO_DRIVER_LOADING\n");
    status = app->bs->load_image(0u, app->image, 0, image_data, image_size,
                                 &app->audio_driver_image);
    (void)app->bs->free_pool(image_data);
    if (EFI_ERROR(status)) return status;
    debug_write("OPENCONSOLE_AUDIO_DRIVER_STARTING\n");
    status = app->bs->start_image(app->audio_driver_image, 0, 0);
    if (EFI_ERROR(status)) {
        (void)app->bs->unload_image(app->audio_driver_image);
        app->audio_driver_image = 0;
        return status;
    }
    debug_write("OPENCONSOLE_AUDIO_DRIVER_STARTED\n");
    status = app->bs->locate_handle_buffer(2u, &pci_io_guid, 0,
                                           &handle_count, &handles);
    if (!EFI_ERROR(status)) {
        for (index = 0u; index < handle_count; ++index) {
            EFI_PCI_IO_PROTOCOL *pci = 0;
            uint32_t config[4] = {0u, 0u, 0u, 0u};
            if (!EFI_ERROR(app->bs->handle_protocol(handles[index], &pci_io_guid,
                                                     (void **)&pci)) && pci != 0 &&
                !EFI_ERROR(pci->pci.read(pci, 2u, 0u, 4u, config)) &&
                (uint8_t)(config[2] >> 24) == 0x04u &&
                (uint8_t)(config[2] >> 16) == 0x03u) {
                debug_write("OPENCONSOLE_AUDIO_DRIVER_CONNECTING_HDA\n");
                (void)app->bs->connect_controller(handles[index], 0, 0, 1u);
                debug_write("OPENCONSOLE_AUDIO_DRIVER_CONNECTED_HDA\n");
            }
        }
    }
    status = app->bs->locate_protocol(&OC_AUDIO_IO_PROTOCOL_GUID, 0, &protocol);
    if (!EFI_ERROR(status)) {
        if (handles != 0) (void)app->bs->free_pool(handles);
        return EFI_SUCCESS;
    }

    /* A started driver that produced no usable output must not remain bound.
     * Disconnect only this image from the HDA controllers we attempted; the
     * driver's Stop callbacks reclaim codec children, DMA and protocols. */
    debug_write("OPENCONSOLE_AUDIO_DRIVER_NO_OUTPUT_CLEANUP\n");
    for (index = 0u; index < handle_count; ++index) {
        EFI_PCI_IO_PROTOCOL *pci = 0;
        uint32_t config[4] = {0u, 0u, 0u, 0u};
        if (!EFI_ERROR(app->bs->handle_protocol(handles[index], &pci_io_guid,
                                                 (void **)&pci)) && pci != 0 &&
            !EFI_ERROR(pci->pci.read(pci, 2u, 0u, 4u, config)) &&
            (uint8_t)(config[2] >> 24) == 0x04u &&
            (uint8_t)(config[2] >> 16) == 0x03u)
            (void)app->bs->disconnect_controller(
                handles[index], app->audio_driver_image, 0);
    }
    if (handles != 0) (void)app->bs->free_pool(handles);
    if (!EFI_ERROR(app->bs->unload_image(app->audio_driver_image)))
        app->audio_driver_image = 0;
    return status;
}

static void install_audio_provider(launcher *app) {
    oc_audio_platform platform;
    EFI_STATUS status = ensure_audioio(app);
    if (EFI_ERROR(status)) {
        debug_write("OPENCONSOLE_AUDIO_OFFLINE_NO_AUDIOIO\n");
        return;
    }
    status = app->bs->allocate_pool(EFI_LOADER_DATA, sizeof(*app->audio_backend),
                                    (void **)&app->audio_backend);
    if (!EFI_ERROR(status))
        status = app->bs->allocate_pool(EFI_LOADER_DATA, sizeof(*app->audio_provider),
                                        (void **)&app->audio_provider);
    if (EFI_ERROR(status)) goto fail;
    debug_write("OPENCONSOLE_AUDIO_BACKEND_INITIALIZING\n");
    status = oc_audioio_backend_initialize(app->audio_backend, app->bs);
    if (EFI_ERROR(status)) goto fail;
    debug_write("OPENCONSOLE_AUDIO_BACKEND_INITIALIZED\n");
    oc_audioio_backend_get_platform(app->audio_backend, &platform);
    status = oc_audio_provider_initialize(app->audio_provider, &platform);
    if (EFI_ERROR(status)) goto fail;
    status = app->bs->install_protocol_interface(
        &app->audio_provider_handle, (EFI_GUID *)&OC_AUDIO_PROTOCOL_GUID,
        EFI_NATIVE_INTERFACE, &app->audio_provider->protocol);
    if (EFI_ERROR(status)) goto fail;
    app->audio_ready = 1u;
    debug_write("OPENCONSOLE_AUDIO_PROVIDER_READY\n");
    return;

fail:
    if (app->audio_provider != 0) {
        oc_audio_provider_shutdown(app->audio_provider);
        (void)app->bs->free_pool(app->audio_provider);
        app->audio_provider = 0;
    }
    if (app->audio_backend != 0) {
        oc_audioio_backend_shutdown(app->audio_backend);
        (void)app->bs->free_pool(app->audio_backend);
        app->audio_backend = 0;
    }
    debug_write("OPENCONSOLE_AUDIO_OFFLINE_BACKEND_FAILED\n");
}
#endif

static void inventory_debug_write(void *context, const char *text) {
    launcher *app = context;
    UINTN size = 0u;
    debug_write(text);
    while (text[size] != '\0') ++size;
    if (app != 0 && app->inventory_log != 0 && size != 0u) {
        UINTN written = size;
        if (EFI_ERROR(app->inventory_log->write(app->inventory_log,&written,text)) ||
            written != size) {
            debug_write("OPENCONSOLE_IO_INVENTORY_LOG_WRITE_FAILED\n");
            (void)app->inventory_log->close(app->inventory_log);
            app->inventory_log = 0;
        }
    }
}

static void open_inventory_log(launcher *app, const CHAR16 *path) {
    EFI_STATUS status = app->root->open(app->root,&app->inventory_log,path,
                                        EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0u);
    if (status == EFI_NOT_FOUND) status = app->root->open(
        app->root,&app->inventory_log,path,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|
        EFI_FILE_MODE_CREATE,0u);
    if (!EFI_ERROR(status)) status = app->inventory_log->set_position(
        app->inventory_log,0u);
    if (EFI_ERROR(status)) {
        if (app->inventory_log != 0) (void)app->inventory_log->close(app->inventory_log);
        app->inventory_log = 0;
        debug_write("OPENCONSOLE_IO_INVENTORY_LOG_OPEN_FAILED\n");
        return;
    }
    debug_write("OPENCONSOLE_IO_INVENTORY_LOG_OPENED\n");
}

static void close_inventory_log(launcher *app) {
    EFI_STATUS status;
    if (app->inventory_log == 0) return;
    status = app->inventory_log->flush(app->inventory_log);
    if (!EFI_ERROR(app->inventory_log->close(app->inventory_log)) &&
        !EFI_ERROR(status)) debug_write("OPENCONSOLE_IO_INVENTORY_LOG_FLUSHED\n");
    else debug_write("OPENCONSOLE_IO_INVENTORY_LOG_FLUSH_FAILED\n");
    app->inventory_log = 0;
}

static EFI_STATUS gles_allocate(void *user, UINTN size, void **out) {
    launcher *app = user;
    return app->bs->allocate_pool(EFI_LOADER_DATA, size, out);
}

static void gles_release(void *user, void *memory) {
    launcher *app = user;
    if (memory != 0) (void)app->bs->free_pool(memory);
}

#if OPENCONSOLE_SLICE_TEST
static EFI_STATUS select_display_mode(launcher *app, uint32_t wanted_width,
                                      uint32_t wanted_height) {
    uint32_t mode;
    if (app == 0 || app->gop == 0 || app->gop->mode == 0 ||
        app->gop->query_mode == 0 || app->gop->set_mode == 0)
        return EFI_UNSUPPORTED;
    for (mode = 0u; mode < app->gop->mode->max_mode; ++mode) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = 0;
        UINTN info_size = 0u;
        EFI_STATUS status = app->gop->query_mode(app->gop, mode, &info_size, &info);
        if (!EFI_ERROR(status) && info != 0 &&
            info->horizontal_resolution == wanted_width &&
            info->vertical_resolution == wanted_height) {
            app->bs->free_pool(info);
            status = app->gop->set_mode(app->gop, mode);
            if (!EFI_ERROR(status)) {
                debug_write("OPENCONSOLE_DISPLAY_MODE_1920X1080\n");
                return EFI_SUCCESS;
            }
            return status;
        }
        if (info != 0) app->bs->free_pool(info);
    }
    debug_write("OPENCONSOLE_DISPLAY_MODE_1920X1080_UNAVAILABLE\n");
    return EFI_NOT_FOUND;
}
#endif

static EFI_STATUS gles_blit(void *user, const void *pixels, uint32_t width, uint32_t height) {
    launcher *app = user;
    return app->gop->blt(app->gop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)(UINTN)pixels,
                         EFI_GRAPHICS_OUTPUT_BLT_BUFFER_TO_VIDEO,
                         0u, 0u, 0u, 0u, width, height, (UINTN)width * 4u);
}

static EFI_STATUS install_gles_provider(launcher *app) {
    OC_GLES_PLATFORM_SERVICES platform;
    EFI_STATUS status;
    platform.user = app;
    platform.allocate = gles_allocate;
    platform.release = gles_release;
    platform.blit_bgra = gles_blit;
    platform.display_width = app->gop->mode->info->horizontal_resolution;
    platform.display_height = app->gop->mode->info->vertical_resolution;
    status = oc_gles_provider_initialize(&app->gles_provider, &platform);
    if (EFI_ERROR(status)) return status;
    status = app->bs->install_protocol_interface(
        &app->gles_handle, (EFI_GUID *)&OC_GLES2_PROTOCOL_GUID,
        EFI_NATIVE_INTERFACE, &app->gles_provider.protocol);
    if (!EFI_ERROR(status)) debug_write("OPENCONSOLE_GLES_PROVIDER_READY\n");
    return status;
}

#if OPENCONSOLE_QEMU_TEST && !OPENCONSOLE_REAL_NET_TEST && !OPENCONSOLE_REAL_AUDIO_TEST && !OPENCONSOLE_INTEGRATED_IO_TEST
static EFI_STATUS install_io_mock_providers(launcher *app) {
    EFI_STATUS status = oc_io_mock_initialize(&app->io_mock);
    if (EFI_ERROR(status)) return status;
    status = app->bs->install_protocol_interface(
        &app->net_handle, (EFI_GUID *)&OC_NET_PROTOCOL_GUID,
        EFI_NATIVE_INTERFACE, &app->io_mock.net);
    if (EFI_ERROR(status)) return status;
    status = app->bs->install_protocol_interface(
        &app->audio_handle, (EFI_GUID *)&OC_AUDIO_PROTOCOL_GUID,
        EFI_NATIVE_INTERFACE, &app->io_mock.audio);
    if (!EFI_ERROR(status)) debug_write("OPENCONSOLE_IO_MOCK_PROVIDERS_READY\n");
    return status;
}
#endif

#if !OPENCONSOLE_QEMU_TEST || OPENCONSOLE_REAL_NET_TEST || OPENCONSOLE_INTEGRATED_IO_TEST
static void *net_allocate(void *context, UINTN size) {
    launcher *app = context;
    void *memory = 0;
    UINTN *header;
    if (size > (UINTN)-1 - sizeof(*header) ||
        EFI_ERROR(app->bs->allocate_pool(EFI_LOADER_DATA, size + sizeof(*header),
                                         &memory))) return 0;
    header = memory;
    *header = size;
    app->net_allocation_bytes += size;
    ++app->net_allocation_count;
    if (app->net_allocation_bytes > app->net_peak_allocation_bytes)
        app->net_peak_allocation_bytes = app->net_allocation_bytes;
    return header + 1;
}

static void net_release(void *context, void *memory) {
    launcher *app = context;
    UINTN *header;
    if (memory == 0) return;
    header = (UINTN *)memory - 1;
    if (app->net_allocation_bytes >= *header) app->net_allocation_bytes -= *header;
    if (app->net_allocation_count != 0u) --app->net_allocation_count;
    (void)app->bs->free_pool(header);
}

static int net_stack_protocol_present(void *context, const EFI_GUID *guid) {
    launcher *app = context;
    void *interface = 0;
    return !EFI_ERROR(app->bs->locate_protocol(guid, 0, &interface));
}

static EFI_STATUS net_stack_load_driver(void *context, const CHAR16 *file_name,
                                        const EFI_GUID *produced_protocol) {
    static const CHAR16 prefix[] = L"EFI\\OpenConsole\\Drivers\\Network\\";
    launcher *app = context;
    CHAR16 path[96];
    UINTN offset = 0u;
    UINTN index = 0u;
    void *image_data = 0;
    UINTN image_size = 0u;
    EFI_HANDLE driver = 0;
    EFI_STATUS status;
    if (app->net_stack_driver_count >= OC_NET_DRIVER_HANDLE_CAPACITY)
        return EFI_OUT_OF_RESOURCES;
    debug_write("OPENCONSOLE_NET_STACK_DRIVER_BEGIN_");
    while (file_name[index] != 0u) debug_putc((char)file_name[index++]);
    debug_putc('\n');
    index = 0u;
    while (prefix[offset] != 0u && offset + 1u < sizeof(path) / sizeof(path[0])) {
        path[offset] = prefix[offset];
        ++offset;
    }
    while (file_name[index] != 0u && offset + 1u < sizeof(path) / sizeof(path[0]))
        path[offset++] = file_name[index++];
    if (file_name[index] != 0u) return EFI_INVALID_PARAMETER;
    path[offset] = 0u;
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    status = read_authorized_platform_image(app,path,OC_PLATFORM_ROLE_NETWORK,
        MAX_FILE_BYTES,&image_data,&image_size);
#else
    status = read_file(app, app->root, path, MAX_FILE_BYTES, &image_data, &image_size);
#endif
    if (EFI_ERROR(status)) return status;
    status = app->bs->load_image(0u, app->image, 0, image_data, image_size, &driver);
    (void)app->bs->free_pool(image_data);
    if (EFI_ERROR(status)) return status;
    debug_write("OPENCONSOLE_NET_STACK_DRIVER_LOADED\n");
    status = app->bs->start_image(driver, 0, 0);
    if (EFI_ERROR(status)) {
        (void)app->bs->unload_image(driver);
        return status;
    }
    debug_write("OPENCONSOLE_NET_STACK_DRIVER_STARTED\n");
    debug_write("OPENCONSOLE_NET_STACK_DRIVER_COUNT_");
    debug_hex_uintn(app->net_stack_driver_count);
    app->net_stack_drivers[app->net_stack_driver_count++] = driver;
    (void)produced_protocol;
    return EFI_SUCCESS;
}

static EFI_STATUS net_stack_connect_loaded(void *context) {
    launcher *app = context;
    EFI_STATUS status = EFI_NOT_FOUND;
    UINTN pass;
    if (app->net_backend->controller == 0) return EFI_NOT_FOUND;
    debug_write("OPENCONSOLE_NET_STACK_CONNECTING\n");
    for (pass = 0u; pass < app->net_stack_driver_count; ++pass) {
        EFI_HANDLE driver_list[2] = {app->net_stack_drivers[pass], 0};
        EFI_STATUS pass_status = app->bs->connect_controller(
            app->net_backend->controller, driver_list, 0, 0u);
        debug_write("OPENCONSOLE_NET_STACK_CONNECT_STATUS_");
        debug_hex_uintn(pass_status);
        if (!EFI_ERROR(pass_status)) status = EFI_SUCCESS;
    }
    if (!EFI_ERROR(app->bs->connect_controller(
            app->net_backend->controller, 0, 0, 1u))) status = EFI_SUCCESS;
    if (!EFI_ERROR(status)) debug_write("OPENCONSOLE_NET_STACK_CONNECTED\n");
    return status;
}

static void load_missing_network_stack(launcher *app) {
    oc_net_stack_loader_ops ops;
    oc_net_stack_load_report report;
    EFI_STATUS status;
    ops.context = app;
    ops.protocol_present = net_stack_protocol_present;
    ops.load_driver = net_stack_load_driver;
    ops.connect_loaded = net_stack_connect_loaded;
    status = oc_net_stack_load_missing(&ops, &report);
    if (EFI_ERROR(status)) {
        debug_write("OPENCONSOLE_NET_STACK_INCOMPLETE\n");
        debug_write("OPENCONSOLE_NET_STACK_FAILED_INDEX_");
        debug_hex_uintn(report.failed_index);
        debug_write("OPENCONSOLE_NET_STACK_FAILED_STATUS_");
        debug_hex_uintn(status);
    } else if (report.loaded == 0u)
        debug_write("OPENCONSOLE_NET_STACK_FIRMWARE_COMPLETE\n");
    else
        debug_write("OPENCONSOLE_NET_STACK_PACKAGED_LOADED\n");
}

static EFI_STATUS snp_wrapper_load(void *context, EFI_HANDLE *image) {
    static const CHAR16 name[] = L"SnpDxe.efi";
    launcher *app = context;
    EFI_STATUS status = net_stack_load_driver(app, name, &simple_network_guid);
    if (!EFI_ERROR(status) && app->net_stack_driver_count != 0u)
        *image = app->net_stack_drivers[app->net_stack_driver_count - 1u];
    return status;
}

static EFI_STATUS snp_wrapper_connect(void *context, EFI_HANDLE controller,
                                      EFI_HANDLE image) {
    launcher *app = context;
    EFI_HANDLE drivers[2] = {image, 0};
    return app->bs->connect_controller(controller, drivers, 0, 0u);
}

static int snp_wrapper_present(void *context, EFI_HANDLE controller) {
    launcher *app = context;
    void *protocol = 0;
    return !EFI_ERROR(app->bs->handle_protocol(
        controller, &simple_network_guid, &protocol)) && protocol != 0;
}

static EFI_STATUS snp_wrapper_disconnect(void *context, EFI_HANDLE controller,
                                         EFI_HANDLE image) {
    launcher *app = context;
    return app->bs->disconnect_controller(controller, image, 0);
}

static EFI_STATUS snp_wrapper_unload(void *context, EFI_HANDLE image) {
    launcher *app = context;
    EFI_STATUS status = app->bs->unload_image(image);
    if (!EFI_ERROR(status) && app->net_stack_driver_count != 0u &&
        app->net_stack_drivers[app->net_stack_driver_count - 1u] == image)
        --app->net_stack_driver_count;
    return status;
}

#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
static UINTN device_path_prefix_size(const uint8_t *path) {
    UINTN used=0u;while(path!=0&&used<=4092u){UINTN node=(UINTN)path[used+2u]|((UINTN)path[used+3u]<<8u);if(node<4u||node>4096u-used)return 0u;if(path[used]==0x7fu)return used;used+=node;}return 0u;
}

static int path_is_child(const uint8_t *parent,const uint8_t *child) {
    UINTN size=device_path_prefix_size(parent),index;if(size==0u||device_path_prefix_size(child)<size)return 0;
    for(index=0u;index<size;index++)if(parent[index]!=child[index])return 0;return 1;
}

static int controller_has_protocol(launcher *app,EFI_HANDLE controller,const EFI_GUID *guid) {
    EFI_HANDLE *handles=0;UINTN count=0u,index;void *controller_path=0;int found=0;
    if(!EFI_ERROR(app->bs->handle_protocol(controller,&device_path_guid,&controller_path))&&controller_path!=0&&
       !EFI_ERROR(app->bs->locate_handle_buffer(EFI_LOCATE_BY_PROTOCOL,guid,0,&count,&handles))){
        for(index=0u;index<count&&!found;index++){void *path=0;if(!EFI_ERROR(app->bs->handle_protocol(handles[index],&device_path_guid,&path))&&path!=0&&path_is_child(controller_path,path))found=1;}
    }
    if(handles!=0)(void)app->bs->free_pool(handles);return found;
}

static EFI_STATUS ethernet_verify(void *context,const oc_eth_candidate *candidate) {
    (void)context;return candidate!=0&&candidate->artifact!=0?EFI_SUCCESS:EFI_SECURITY_VIOLATION;
}

static int ethernet_path(const oc_eth_catalog_entry *entry,CHAR16 out[100]) {
    UINTN index;if(entry==0||entry->path_size+2u>100u)return 0;out[0]='\\';
    for(index=0u;index<entry->path_size;index++)out[index+1u]=(CHAR16)(entry->path[index]=='/'?'\\':entry->path[index]);
    out[entry->path_size+1u]=0u;return 1;
}

static EFI_STATUS ethernet_load(void *context,const oc_eth_candidate *candidate,EFI_HANDLE *image) {
    launcher *app=context;const oc_eth_catalog_entry *entry=candidate->artifact;CHAR16 path[100];
    void *bytes=0;UINTN size=0u,index;uint8_t digest[32];oc_pe_info pe;EFI_STATUS status;
    if(image==0||!ethernet_path(entry,path)||candidate->artifact_size!=entry->artifact_size)return EFI_INVALID_PARAMETER;
    status=read_file(app,app->root,path,4u*1024u*1024u,&bytes,&size);if(EFI_ERROR(status))return status;
    if(size!=entry->artifact_size||!oc_sha256(bytes,size,digest)){app->bs->free_pool(bytes);return EFI_SECURITY_VIOLATION;}
    for(index=0u;index<32u;index++)if(digest[index]!=entry->sha256[index])break;
    if(index!=32u||oc_pe_validate_native_efi(bytes,size,4u*1024u*1024u,OC_PE_ROLE_DRIVER,&pe)!=OC_PE_OK){app->bs->free_pool(bytes);return EFI_SECURITY_VIOLATION;}
    status=app->bs->load_image(0u,app->image,0,bytes,size,image);app->bs->free_pool(bytes);if(EFI_ERROR(status))return status;
    status=app->bs->start_image(*image,0,0);if(EFI_ERROR(status)){app->bs->unload_image(*image);*image=0;return status;}
    if(app->net_stack_driver_count>=OC_NET_DRIVER_HANDLE_CAPACITY){app->bs->unload_image(*image);*image=0;return EFI_OUT_OF_RESOURCES;}
    app->net_stack_drivers[app->net_stack_driver_count++]=*image;return EFI_SUCCESS;
}

static EFI_STATUS ethernet_connect(void *context,EFI_HANDLE controller,EFI_HANDLE image) {
    launcher *app=context;EFI_HANDLE drivers[2]={image,0};return app->bs->connect_controller(controller,drivers,0,0u);
}

static EFI_STATUS ethernet_probe(void *context,EFI_HANDLE controller,uint32_t expected) {
    launcher *app=context;if((expected&OC_ETH_PROTOCOL_SNP)&&!controller_has_protocol(app,controller,&simple_network_guid))return EFI_NOT_FOUND;
    if((expected&(OC_ETH_PROTOCOL_NII|OC_ETH_PROTOCOL_UNDI))&&!controller_has_protocol(app,controller,&nii31_guid))return EFI_NOT_FOUND;
    return EFI_SUCCESS;
}

static EFI_STATUS ethernet_disconnect(void *context,EFI_HANDLE controller,EFI_HANDLE image) {
    launcher *app=context;return app->bs->disconnect_controller(controller,image,0);
}

static EFI_STATUS ethernet_unload(void *context,EFI_HANDLE image) {
    launcher *app=context;UINTN index;EFI_STATUS status=app->bs->unload_image(image);if(EFI_ERROR(status))return status;
    for(index=0u;index<app->net_stack_driver_count;index++)if(app->net_stack_drivers[index]==image){for(;index+1u<app->net_stack_driver_count;index++)app->net_stack_drivers[index]=app->net_stack_drivers[index+1u];--app->net_stack_driver_count;break;}return EFI_SUCCESS;
}

static void ethernet_emit(void *context,oc_eth_diagnostic diagnostic,UINTN controller,UINTN candidate,EFI_STATUS status) {
    (void)context;debug_write("OPENCONSOLE_ETH_SELECT_");debug_hex_uintn((UINTN)diagnostic);debug_write("OPENCONSOLE_ETH_CONTROLLER_");debug_hex_uintn(controller);debug_write("OPENCONSOLE_ETH_CANDIDATE_");debug_hex_uintn(candidate);debug_write("OPENCONSOLE_ETH_STATUS_");debug_hex_uintn((UINTN)status);
}

static void load_ethernet_hardware_drivers(launcher *app) {
    EFI_HANDLE *handles=0;UINTN count=0u,index,used=0u;oc_eth_controller controllers[OC_ETH_MAX_CONTROLLERS];oc_eth_selection_report report;
    oc_eth_selector_ops ops={app,ethernet_verify,ethernet_load,ethernet_connect,ethernet_probe,ethernet_disconnect,ethernet_unload,ethernet_emit};
    oc_eth_architecture architecture=OC_TARGET_ARCH_X64?OC_ETH_ARCH_X64:OC_ETH_ARCH_ARM64;
    if(EFI_ERROR(load_ethernet_catalog(app)))return;
    if(EFI_ERROR(app->bs->locate_handle_buffer(EFI_LOCATE_BY_PROTOCOL,&pci_io_guid,0,&count,&handles)))return;
    for(index=0u;index<count&&used<OC_ETH_MAX_CONTROLLERS;index++){EFI_PCI_IO_PROTOCOL *pci=0;uint32_t config[12];UINTN word;
        if(EFI_ERROR(app->bs->handle_protocol(handles[index],&pci_io_guid,(void **)&pci))||pci==0||EFI_ERROR(pci->pci.read(pci,2u,0u,12u,config)))continue;
        if((config[2]>>24u)!=0x02u||((config[2]>>16u)&0xffu)!=0x00u)continue;
        controllers[used].handle=handles[index];controllers[used].vendor=(uint16_t)config[0];controllers[used].device=(uint16_t)(config[0]>>16u);
        controllers[used].revision=(uint8_t)config[2];controllers[used].subsystem_vendor=(uint16_t)config[11];controllers[used].subsystem_device=(uint16_t)(config[11]>>16u);
        word=(UINTN)controller_has_protocol(app,handles[index],&simple_network_guid);controllers[used].firmware_snp_present=(uint8_t)word;controllers[used].firmware_snp_usable=(uint8_t)word;++used;
    }
    app->bs->free_pool(handles);
    (void)oc_eth_select_drivers(controllers,used,app->ethernet_candidates,
        app->ethernet_candidate_count,architecture,&ops,&report);
}
#endif

static EFI_STATUS ensure_snp_from_nii(launcher *app) {
    EFI_HANDLE *nii = 0;
    UINTN count = 0u;
    void *existing = 0;
    oc_net_snp_bridge_report report;
    oc_net_snp_bridge_ops ops = {app, snp_wrapper_load, snp_wrapper_connect,
        snp_wrapper_present, snp_wrapper_disconnect, snp_wrapper_unload};
    EFI_STATUS locate = app->bs->locate_handle_buffer(
        EFI_LOCATE_BY_PROTOCOL, &nii31_guid, 0, &count, &nii);
    int has_snp = !EFI_ERROR(app->bs->locate_protocol(
        &simple_network_guid, 0, &existing)) && existing != 0;
    EFI_STATUS status = oc_net_bridge_nii_to_snp(has_snp,
        EFI_ERROR(locate) ? 0 : nii, EFI_ERROR(locate) ? 0u : count,
        &ops, &report);
    if (nii != 0) (void)app->bs->free_pool(nii);
    if (!EFI_ERROR(status) && report.wrapper_image != 0)
        debug_write("OPENCONSOLE_NET_SNP_WRAPPER_SELECTED\n");
    else if (has_snp)
        debug_write("OPENCONSOLE_NET_SNP_FIRMWARE_PRESENT\n");
    else
        debug_write("OPENCONSOLE_NET_SNP_WRAPPER_UNAVAILABLE\n");
    return status;
}

static void install_network_provider(launcher *app) {
    oc_net_platform platform;
    EFI_STATUS status = app->bs->allocate_pool(
        EFI_LOADER_DATA, sizeof(*app->net_backend), (void **)&app->net_backend);
    if (!EFI_ERROR(status)) status = app->bs->allocate_pool(
        EFI_LOADER_DATA, sizeof(*app->net_provider), (void **)&app->net_provider);
    if (EFI_ERROR(status)) {
        if (app->net_backend != 0) (void)app->bs->free_pool(app->net_backend);
        app->net_backend = 0;
        app->net_provider = 0;
        debug_write("OPENCONSOLE_NET_OFFLINE_NO_MEMORY\n");
        return;
    }
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    load_ethernet_hardware_drivers(app);
#endif
    (void)ensure_snp_from_nii(app);
    status = oc_uefi_net_backend_initialize(app->net_backend, app->bs);
    if (EFI_ERROR(status)) {
        (void)app->bs->free_pool(app->net_provider);
        (void)app->bs->free_pool(app->net_backend);
        app->net_backend = 0;
        app->net_provider = 0;
        debug_write("OPENCONSOLE_NET_OFFLINE_NO_BACKEND\n");
        return;
    }
    load_missing_network_stack(app);
    status = oc_uefi_net_backend_initialize(app->net_backend, app->bs);
    if (EFI_ERROR(status)) {
        (void)app->bs->free_pool(app->net_provider);
        (void)app->bs->free_pool(app->net_backend);
        app->net_backend = 0;
        app->net_provider = 0;
        debug_write("OPENCONSOLE_NET_OFFLINE_STACK_REPROBE_FAILED\n");
        return;
    }
    zero_bytes(&platform, sizeof(platform));
    platform.context = app;
    platform.allocate = net_allocate;
    platform.release = net_release;
    oc_uefi_net_backend_get_platform(app->net_backend, &platform.backend);
    status = oc_net_provider_initialize(app->net_provider, &platform);
    if (EFI_ERROR(status)) {
        (void)app->bs->free_pool(app->net_provider);
        (void)app->bs->free_pool(app->net_backend);
        app->net_backend = 0;
        app->net_provider = 0;
        debug_write("OPENCONSOLE_NET_OFFLINE_PROVIDER_FAILED\n");
        return;
    }
    status = app->bs->install_protocol_interface(
        &app->net_provider_handle, (EFI_GUID *)&OC_NET_PROTOCOL_GUID,
        EFI_NATIVE_INTERFACE, &app->net_provider->protocol);
    if (EFI_ERROR(status)) {
        oc_net_provider_shutdown(app->net_provider);
        (void)app->bs->free_pool(app->net_provider);
        (void)app->bs->free_pool(app->net_backend);
        app->net_backend = 0;
        app->net_provider = 0;
        debug_write("OPENCONSOLE_NET_OFFLINE_INSTALL_FAILED\n");
        return;
    }
    app->net_ready = 1u;
    debug_write("OPENCONSOLE_NET_PROVIDER_READY\n");
}

static EFI_STATUS await_net_operation(launcher *app, OC_NET_SESSION *session,
                                      OC_NET_OPERATION *operation,
                                      uint64_t *now_usec,
                                      OC_NET_OPERATION_STATUS *result) {
    for (;;) {
        EFI_STATUS status;
        result->structure_size = sizeof(*result);
        status = app->net_provider->protocol.query_operation(operation, result);
        if (EFI_ERROR(status) || result->result != OC_NET_RESULT_PENDING) return status;
        *now_usec += 1000u;
        status = app->net_provider->protocol.pump(session, *now_usec);
        if (EFI_ERROR(status)) return status;
        (void)app->bs->stall(1000u);
    }
}

static void probe_online(launcher *app) {
    static const char request[] = "openconsole-online-probe";
    static const char expected[] = "openconsole-udp-reply";
    OC_NET_PROTOCOL *net;
    OC_NET_SESSION *session = 0;
    OC_NET_OPERATION *operation = 0;
    OC_NET_SESSION_DESC desc = {sizeof(desc), OC_NET_ABI_VERSION, 4096u, 2u, 0u};
    OC_NET_OPERATION_STATUS result;
    OC_NET_DEADLINE deadline = {sizeof(deadline), 0u, 15000000u};
    OC_NET_TRANSFER_DESC transfer;
    uint8_t response[64];
    uint64_t now = 0u;
    EFI_STATUS status;
    UINTN i;
    if (!app->net_ready || app->net_provider == 0) return;
    net = &app->net_provider->protocol;
    ++app->net_generation;
    status = oc_net_provider_begin_generation(app->net_provider, app->net_generation);
    if (EFI_ERROR(status)) goto done;
    status = net->create_session(&desc, &session);
    if (EFI_ERROR(status)) goto done;
    status = net->configure_dhcp4(session, &deadline, &operation);
    if (EFI_ERROR(status)) goto done;
    status = await_net_operation(app, session, operation, &now, &result);
    if (EFI_ERROR(status) || result.result != OC_NET_RESULT_OK) goto done;
    (void)net->release_operation(operation); operation = 0;
    transfer = (OC_NET_TRANSFER_DESC){sizeof(transfer),
        (uint32_t)10u | ((uint32_t)2u << 16) | ((uint32_t)2u << 24),
        9000u, 0u, request, sizeof(request) - 1u, response, sizeof(response),
        {sizeof(OC_NET_DEADLINE), 0u, now + 5000000u}};
    status = net->start_udp4(session, &transfer, &operation);
    if (EFI_ERROR(status)) goto done;
    status = await_net_operation(app, session, operation, &now, &result);
    if (EFI_ERROR(status) || result.result != OC_NET_RESULT_OK ||
        result.bytes_transferred != sizeof(expected) - 1u) goto done;
    for (i = 0u; i < sizeof(expected) - 1u; ++i)
        if (response[i] != (uint8_t)expected[i]) goto done;
    app->net_online = 1u;
    debug_write("OPENCONSOLE_CONNECTIVITY_ONLINE\n");
done:
    if (operation != 0) (void)net->release_operation(operation);
    if (session != 0) (void)net->destroy_session(session);
    oc_net_provider_cleanup_generation(app->net_provider, app->net_generation);
    if (!app->net_online) debug_write("OPENCONSOLE_CONNECTIVITY_PROBE_FAILED\n");
}
#endif

static void debug_putc(char value) {
    oc_platform_debug_putc(value);
}

static void debug_write(const char *text) {
    while (*text != '\0') debug_putc(*text++);
}

static void debug_hex16(uint16_t value) {
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    for (shift = 12; shift >= 0; shift -= 4) debug_putc(digits[(value >> shift) & 15u]);
}

static void debug_hex_uintn(UINTN value) {
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    for (shift = (int)(sizeof(UINTN) * 8u) - 4; shift >= 0; shift -= 4)
        debug_putc(digits[(value >> shift) & 15u]);
    debug_putc('\n');
}

static void report_amd_pci(launcher *app) {
    oc_pci_device devices[8];
    UINTN count = 0u;
    UINTN i;
    if (EFI_ERROR(oc_probe_amd_pci(app->bs, devices, 8u, &count))) {
        debug_write("OPENCONSOLE_AMD_PCI_PROBE_FAILED\n");
        return;
    }
    for (i = 0u; i < count; ++i) {
        debug_write("OPENCONSOLE_AMD_PCI_");
        debug_hex16(devices[i].vendor_id);
        debug_putc('_');
        debug_hex16(devices[i].device_id);
        debug_putc('\n');
    }
}

#if OPENCONSOLE_QEMU_TEST
static void __attribute__((unused)) qemu_exit(uint32_t value) {
    oc_platform_qemu_exit(value);
}
#endif

static void zero_bytes(void *destination, UINTN size) {
    uint8_t *bytes = destination;
    while (size-- != 0u) *bytes++ = 0u;
}

static int char16_equal(const CHAR16 *left, const CHAR16 *right) {
    UINTN i = 0u;
    while (left[i] != 0u && left[i] == right[i]) ++i;
    return left[i] == right[i];
}

static int ascii_equal(const char *left, const char *right) {
    while (*left != '\0' && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static void evaluate_game_support(launcher *app, game_entry *game) {
    UINTN i;
    OC_GLES_CAPABILITIES caps = {sizeof(caps), 0u, 0u, 0u, 0u, {0u}};
    oc_host_capabilities host = {1u, 1u, (uint8_t)(app->pointer != 0), 0u,
                                 (uint8_t)app->net_ready, (uint8_t)app->audio_ready};
    game->host_supported = 1u;
    if (game->manifest.graphics_api == OC_GRAPHICS_GLES2) {
        if (EFI_ERROR(app->gles_provider.protocol.query_capabilities(&caps))) {
            game->host_supported = 0u;
        } else {
            host.gles2 = 1u;
            if (game->manifest.min_graphics_abi > caps.abi_version)
                game->host_supported = 0u;
        }
        game->hardware_accel_supported =
            (caps.backend_flags & OC_GLES_CONTEXT_ACCELERATED) != 0u;
        if (game->manifest.accel_policy == OC_ACCEL_REQUIRED &&
            !game->hardware_accel_supported)
            game->host_supported = 0u;
    }
    if (!oc_manifest_required_capabilities_supported(&game->manifest, &host))
        game->host_supported = 0u;
    /* Deprecated v1 compatibility is deliberately separate from v2
     * capability negotiation and can be removed with the v1 policy. */
    for (i = 0u; i < game->manifest.required_protocol_count; ++i) {
        const char *requirement = game->manifest.required_protocols[i];
        if (ascii_equal(requirement, "input.keyboard")) continue;
        if (ascii_equal(requirement, "openconsole.gles2") ||
            ascii_equal(requirement, "suitcase.gles")) continue;
        if (ascii_equal(requirement, "openconsole.net")) {
            if (!app->net_ready) game->host_supported = 0u;
        } else if (ascii_equal(requirement, "openconsole.audio")) {
            if (!app->audio_ready) game->host_supported = 0u;
        } else game->host_supported = 0u;
    }
}

static int char16_copy(CHAR16 *out, UINTN capacity, const CHAR16 *source) {
    UINTN i = 0u;
    while (source[i] != 0u) {
        if (i + 1u >= capacity) return 0;
        out[i] = source[i];
        ++i;
    }
    out[i] = 0u;
    return 1;
}

static int ascii_path_to_char16(CHAR16 *out, UINTN capacity, const char *source) {
    UINTN i = 0u;
    while (source[i] != '\0') {
        unsigned char value = (unsigned char)source[i];
        if (value >= 0x80u || i + 1u >= capacity) return 0;
        out[i] = value == '/' ? (CHAR16)'\\' : (CHAR16)value;
        ++i;
    }
    out[i] = 0u;
    return 1;
}

#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
typedef struct { uint8_t type, subtype, length[2]; } oc_device_path_node;

static EFI_STATUS make_file_device_path(launcher *app, const CHAR16 *path, void **result) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    oc_device_path_node *base = 0, *node;
    uint8_t *out;
    UINTN base_size = 0u, path_chars = 0u, file_size, total, length;
    EFI_STATUS status;
    *result = 0;
    status = app->bs->handle_protocol(app->image, &loaded_image_guid, (void **)&loaded);
    if (EFI_ERROR(status) || loaded == 0) return EFI_NOT_FOUND;
    status = app->bs->handle_protocol(loaded->device_handle, &device_path_guid, (void **)&base);
    if (EFI_ERROR(status) || base == 0) return EFI_NOT_FOUND;
    node = base;
    for (;;) {
        length = (UINTN)node->length[0] | ((UINTN)node->length[1] << 8u);
        if (length < sizeof(*node) || base_size > 4096u - length)
            return EFI_SECURITY_VIOLATION;
        if (node->type == 0x7fu && node->subtype == 0xffu) break;
        base_size += length;
        node = (oc_device_path_node *)((uint8_t *)node + length);
    }
    while (path[path_chars] != 0u) ++path_chars;
    file_size = sizeof(oc_device_path_node) + (path_chars + 1u) * sizeof(CHAR16);
    total = base_size + file_size + sizeof(oc_device_path_node);
    if (file_size > 0xffffu || EFI_ERROR(app->bs->allocate_pool(EFI_LOADER_DATA,
            total, result))) return EFI_OUT_OF_RESOURCES;
    out = *result;
    copy_bytes(out, base, base_size);
    node = (oc_device_path_node *)(out + base_size);
    node->type = 0x04u; node->subtype = 0x04u;
    node->length[0] = (uint8_t)file_size; node->length[1] = (uint8_t)(file_size >> 8u);
    copy_bytes(out + base_size + sizeof(*node), path,
               (path_chars + 1u) * sizeof(CHAR16));
    node = (oc_device_path_node *)(out + base_size + file_size);
    node->type = 0x7fu; node->subtype = 0xffu;
    node->length[0] = sizeof(*node); node->length[1] = 0u;
    return EFI_SUCCESS;
}
#endif

static EFI_STATUS open_boot_volume(launcher *app) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_STATUS status = app->bs->handle_protocol(
        app->image, &loaded_image_guid, (void **)&loaded);
    if (EFI_ERROR(status)) return status;
    status = app->bs->handle_protocol(
        loaded->device_handle, &simple_fs_guid, (void **)&fs);
    if (EFI_ERROR(status)) return status;
    status = fs->open_volume(fs, &app->root);
    if (!EFI_ERROR(status)) app->boot_root = app->root;
    return status;
}

#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST) || defined(OPENCONSOLE_DATA_VOLUME_TEST)
static int marker_hex(uint8_t value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

static int valid_data_volume_marker(const uint8_t *bytes, UINTN size) {
    static const char prefix[] = "{\"installationId\":\"";
    static const char suffix[] = "\",\"schema\":\"openconsole.data-volume.v1\"}\n";
    UINTN i, offset = sizeof(prefix) - 1u;
    if (size != (sizeof(prefix) - 1u) + 36u + (sizeof(suffix) - 1u)) return 0;
    for (i = 0u; i < sizeof(prefix) - 1u; ++i) if (bytes[i] != (uint8_t)prefix[i]) return 0;
    for (i = 0u; i < 36u; ++i) {
        uint8_t value = bytes[offset + i];
        if (i == 8u || i == 13u || i == 18u || i == 23u) {
            if (value != '-') return 0;
        } else if (!marker_hex(value)) return 0;
    }
    if (bytes[offset + 14u] != '4' ||
        !(bytes[offset + 19u] == '8' || bytes[offset + 19u] == '9' ||
          bytes[offset + 19u] == 'a' || bytes[offset + 19u] == 'b')) return 0;
    offset += 36u;
    for (i = 0u; i < sizeof(suffix) - 1u; ++i) if (bytes[offset + i] != (uint8_t)suffix[i]) return 0;
    return 1;
}

static EFI_STATUS select_data_volume(launcher *app) {
    static const CHAR16 marker_path[] = L"\\EFI\\OpenConsole\\data-volume.json";
    EFI_HANDLE *handles = 0;
    UINTN count = 0u, index, matches = 0u;
    EFI_FILE_PROTOCOL *selected = 0;
    EFI_STATUS status = app->bs->locate_handle_buffer(EFI_LOCATE_BY_PROTOCOL,
        &simple_fs_guid, 0, &count, &handles);
    if (EFI_ERROR(status)) return EFI_SUCCESS;
    if (count > 64u) count = 64u;
    for (index = 0u; index < count; ++index) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
        EFI_FILE_PROTOCOL *candidate = 0;
        void *marker = 0;
        UINTN marker_size = 0u;
        if (EFI_ERROR(app->bs->handle_protocol(handles[index], &simple_fs_guid,
                (void **)&fs)) || fs == 0 || EFI_ERROR(fs->open_volume(fs, &candidate))) continue;
        status = read_file(app, candidate, marker_path, 512u, &marker, &marker_size);
        if (!EFI_ERROR(status) && valid_data_volume_marker(marker, marker_size)) {
            ++matches;
            if (selected != 0) selected->close(selected);
            selected = candidate;
            candidate = 0;
        }
        if (marker != 0) app->bs->free_pool(marker);
        if (candidate != 0) candidate->close(candidate);
    }
    app->bs->free_pool(handles);
    if (matches == 1u) {
        app->root = selected;
        debug_write("OPENCONSOLE_DATA_VOLUME_SELECTED\n");
    } else {
        if (selected != 0) selected->close(selected);
        app->root = app->boot_root;
        if (matches > 1u) {
            app->data_volume_ambiguous = 1u;
            debug_write("OPENCONSOLE_DATA_VOLUME_AMBIGUOUS_MUTATIONS_DISABLED\n");
        } else debug_write("OPENCONSOLE_DATA_VOLUME_BOOT_FALLBACK\n");
    }
    return EFI_SUCCESS;
}
#endif

static EFI_STATUS read_file(launcher *app, EFI_FILE_PROTOCOL *directory,
                            const CHAR16 *path, UINTN limit,
                            void **data, UINTN *data_size) {
    EFI_FILE_PROTOCOL *file = 0;
    EFI_FILE_INFO *info = 0;
    UINTN info_size = 0u;
    UINTN read_size;
    EFI_STATUS status;
    *data = 0;
    *data_size = 0u;
    status = directory->open(directory, &file, path, EFI_FILE_MODE_READ, 0u);
    if (EFI_ERROR(status)) return status;
    status = file->get_info(file, &file_info_guid, &info_size, 0);
    if (status != EFI_BUFFER_TOO_SMALL || info_size < sizeof(EFI_FILE_INFO)) goto done;
    status = app->bs->allocate_pool(EFI_LOADER_DATA, info_size, (void **)&info);
    if (EFI_ERROR(status)) goto done;
    status = file->get_info(file, &file_info_guid, &info_size, info);
    if (EFI_ERROR(status)) goto done;
    if (info->file_size > limit || info->file_size > (uint64_t)(~(UINTN)0) - 1u) {
        status = EFI_BUFFER_TOO_SMALL;
        goto done;
    }
    read_size = (UINTN)info->file_size;
    status = app->bs->allocate_pool(EFI_LOADER_DATA, read_size + 1u, data);
    if (EFI_ERROR(status)) goto done;
    status = file->read(file, &read_size, *data);
    if (EFI_ERROR(status) || read_size != (UINTN)info->file_size) {
        app->bs->free_pool(*data);
        *data = 0;
        if (!EFI_ERROR(status)) status = EFI_NOT_FOUND;
        goto done;
    }
    ((uint8_t *)*data)[read_size] = 0u;
    *data_size = read_size;
done:
    if (info != 0) app->bs->free_pool(info);
    if (file != 0) file->close(file);
    return status;
}

static EFI_STATUS measure_directory(launcher *app, EFI_FILE_PROTOCOL *directory,
                                    UINTN depth, oc_package_measure *measure) {
    static const CHAR16 dot[] = L".";
    static const CHAR16 dotdot[] = L"..";
    EFI_FILE_INFO *info = 0;
    UINTN capacity = 2048u;
    EFI_STATUS status;
    if (depth > measure->max_depth) return EFI_OUT_OF_RESOURCES;
    status = app->bs->allocate_pool(EFI_LOADER_DATA, capacity, (void **)&info);
    if (EFI_ERROR(status)) return status;
    status = directory->set_position(directory, 0u);
    while (!EFI_ERROR(status)) {
        UINTN size = capacity;
        status = directory->read(directory, &size, info);
        if (EFI_ERROR(status) || size == 0u) break;
        if (char16_equal(info->file_name, dot) || char16_equal(info->file_name, dotdot))
            continue;
        if (!oc_package_measure_visit(measure, (uint32_t)depth,
                (info->attribute & EFI_FILE_DIRECTORY) != 0u,
                info->file_size)) {
            status = EFI_OUT_OF_RESOURCES;
            break;
        }
        if ((info->attribute & EFI_FILE_DIRECTORY) != 0u) {
            EFI_FILE_PROTOCOL *child = 0;
            status = directory->open(directory, &child, info->file_name,
                                     EFI_FILE_MODE_READ, 0u);
            if (EFI_ERROR(status)) break;
            status = measure_directory(app, child, depth + 1u, measure);
            child->close(child);
            if (EFI_ERROR(status)) break;
        }
    }
    app->bs->free_pool(info);
    if (!EFI_ERROR(status) && status != EFI_SUCCESS) return EFI_SUCCESS;
    return status;
}

#if !defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
static void load_game_from_directory(launcher *app, EFI_FILE_PROTOCOL *games_dir,
                                     const CHAR16 *directory_name) {
    static const CHAR16 manifest_name[] = L"game.toml";
    static const CHAR16 jacket_name[] = L"jacket.ocj";
    EFI_FILE_PROTOCOL *game_dir = 0;
    void *manifest_data = 0;
    UINTN manifest_size = 0u;
    game_entry *game;
    oc_manifest_result parsed;
    EFI_STATUS status;
    if (app->game_count == MAX_GAMES) return;
    status = games_dir->open(games_dir, &game_dir, directory_name,
                             EFI_FILE_MODE_READ, 0u);
    if (EFI_ERROR(status)) return;
    status = read_file(app, game_dir, manifest_name, OC_MANIFEST_MAX_BYTES,
                       &manifest_data, &manifest_size);
    if (EFI_ERROR(status)) {
        game_dir->close(game_dir);
        return;
    }
    game = &app->games[app->game_count];
    zero_bytes(game, sizeof(*game));
    parsed = oc_manifest_parse(manifest_data, manifest_size, &game->manifest);
    game->manifest_hash = hash_bytes(manifest_data, manifest_size);
    app->bs->free_pool(manifest_data);
    if (parsed.code != OC_MANIFEST_OK ||
        !char16_copy(game->directory, sizeof(game->directory) / sizeof(game->directory[0]),
                     directory_name)) {
        game_dir->close(game_dir);
        return;
    }
    status = read_file(app, game_dir, jacket_name, 1024u * 1024u,
                       &game->jacket, &game->jacket_size);
    if (EFI_ERROR(status)) {
        game->jacket = 0;
        game->jacket_size = 0u;
    }
    {
        oc_package_measure measure;
        oc_package_measure_init(&measure, 8u, 1024u);
        if (!EFI_ERROR(measure_directory(app, game_dir, 0u, &measure)))
            game->package_size = measure.total_bytes;
    }
    evaluate_game_support(app, game);
    ++app->game_count;
    game_dir->close(game_dir);
}
#endif

#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
static int game_id_from_char16(const CHAR16 *source, char out[OC_GAME_ID_MAX + 1u]) {
    UINTN i = 0u;
    if (source == 0) return 0;
    while (source[i] != 0u) {
        if (i >= OC_GAME_ID_MAX || source[i] >= 0x80u) return 0;
        out[i] = (char)source[i];
        ++i;
    }
    out[i] = '\0';
    return oc_delete_component_valid(out);
}

static void load_transactional_game(launcher *app, const CHAR16 *directory_name) {
    static const CHAR16 manifest_name[] = L"game.toml";
    static const CHAR16 jacket_name[] = L"jacket.ocj";
    char game_id[OC_GAME_ID_MAX + 1u], package_path[OC_GAME_STORAGE_PATH_MAX];
    CHAR16 package_path16[OC_GAME_STORAGE_PATH_MAX];
    oc_game_selection selection;
    oc_game_storage_result recovered;
    EFI_FILE_PROTOCOL *package = 0;
    void *manifest_data = 0;
    UINTN manifest_size = 0u;
    size_t cleanup_deletes = 0u;
    game_entry *game;
    oc_manifest_result parsed;
    EFI_STATUS status;
    int quarantined;
    if (!app->game_storage_ready || app->game_count == MAX_GAMES ||
        !game_id_from_char16(directory_name, game_id)) return;
    recovered = oc_game_transaction_recover(&app->game_storage.operations, game_id,
        OC_GAME_GC_MAX_DELETES, &cleanup_deletes, &selection);
    if (recovered != OC_GAME_STORAGE_OK) {
        debug_write("OPENCONSOLE_GAME_RECOVERY_FAILED_ID_");
        debug_write(game_id); debug_write("\n");
        return;
    }
    debug_write("OPENCONSOLE_GAME_RECOVERY_GENERATION_");
    debug_hex_uintn((UINTN)selection.generation);
    debug_write("OPENCONSOLE_GAME_RECOVERY_CLEANUP_COUNT_");
    debug_hex_uintn((UINTN)cleanup_deletes);
    if (selection.tombstone) {
        debug_write("OPENCONSOLE_GAME_RECOVERY_TOMBSTONE_ID_");
        debug_write(game_id); debug_write("\n");
        return;
    }
    quarantined = app->launch_record_ready &&
        oc_launch_record_quarantines(&app->launch_record, selection.bundle_id);
    if (oc_game_package_path(game_id, selection.slot, "game.toml", package_path,
            sizeof(package_path)) != OC_GAME_STORAGE_OK) return;
    {
        UINTN n = 0u;
        while (package_path[n] != '\0') ++n;
        while (n != 0u && package_path[n - 1u] != '\\') --n;
        if (n == 0u) return;
        package_path[n - 1u] = '\0';
    }
    if (!ascii_path_to_char16(package_path16,
            sizeof(package_path16) / sizeof(package_path16[0]), package_path)) return;
    status = app->root->open(app->root, &package, package_path16, EFI_FILE_MODE_READ, 0u);
    if (EFI_ERROR(status)) return;
    status = read_file(app, package, manifest_name, OC_MANIFEST_MAX_BYTES,
                       &manifest_data, &manifest_size);
    if (EFI_ERROR(status)) goto done;
    game = &app->games[app->game_count];
    zero_bytes(game, sizeof(*game));
    parsed = oc_manifest_parse(manifest_data, manifest_size, &game->manifest);
    game->manifest_hash = hash_bytes(manifest_data, manifest_size);
    app->bs->free_pool(manifest_data); manifest_data = 0;
    if (parsed.code != OC_MANIFEST_OK ||
        !directory_matches_id(directory_name, game->manifest.id) ||
        !char16_copy(game->directory,
            sizeof(game->directory) / sizeof(game->directory[0]), directory_name)) goto done;
    status = read_file(app, package, jacket_name, 1024u * 1024u,
                       &game->jacket, &game->jacket_size);
    if (EFI_ERROR(status)) { game->jacket = 0; game->jacket_size = 0u; }
    {
        oc_package_measure measure;
        oc_package_measure_init(&measure, 8u, 1024u);
        if (!EFI_ERROR(measure_directory(app, package, 0u, &measure)))
            game->package_size = measure.total_bytes;
    }
    game->transactional = 1u;
    game->quarantined = quarantined ? 1u : 0u;
    game->selection = selection;
    evaluate_game_support(app, game);
    if (game->quarantined) {
        game->host_supported = 0u;
        debug_write("OPENCONSOLE_GAME_VISIBLE_QUARANTINED_ID_");
        debug_write(game_id); debug_write("\n");
    }
    ++app->game_count;
    debug_write("OPENCONSOLE_GAME_TRANSACTION_VERIFIED_ID_");
    debug_write(game_id); debug_write("\n");
done:
    if (manifest_data != 0) app->bs->free_pool(manifest_data);
    package->close(package);
}
#endif

static EFI_STATUS discover_games(launcher *app) {
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    static const CHAR16 games_name[] = L"\\EFI\\OpenConsole\\State\\Games";
#else
    static const CHAR16 games_name[] = L"games";
#endif
    static const CHAR16 dot[] = L".";
    static const CHAR16 dotdot[] = L"..";
    EFI_FILE_PROTOCOL *games_dir = 0;
    EFI_FILE_INFO *entry = 0;
    UINTN entry_capacity = 2048u;
    EFI_STATUS status = app->root->open(app->root, &games_dir, games_name,
                                        EFI_FILE_MODE_READ, 0u);
    if (EFI_ERROR(status)) return status;
    status = app->bs->allocate_pool(EFI_LOADER_DATA, entry_capacity, (void **)&entry);
    if (EFI_ERROR(status)) {
        games_dir->close(games_dir);
        return status;
    }
    for (;;) {
        UINTN entry_size = entry_capacity;
        status = games_dir->read(games_dir, &entry_size, entry);
        if (EFI_ERROR(status) || entry_size == 0u) break;
        if ((entry->attribute & EFI_FILE_DIRECTORY) != 0u &&
            !char16_equal(entry->file_name, dot) &&
            !char16_equal(entry->file_name, dotdot)) {
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
            load_transactional_game(app, entry->file_name);
#else
            load_game_from_directory(app, games_dir, entry->file_name);
#endif
        }
    }
    app->bs->free_pool(entry);
    games_dir->close(games_dir);
    if (!EFI_ERROR(status)) {
        UINTN i;
        for (i = 1u; i < app->game_count; ++i) {
            game_entry value;
            UINTN j = i;
            copy_bytes(&value, &app->games[i], sizeof(value));
            while (j > 0u) {
                const char *left = app->games[j - 1u].manifest.title;
                const char *right = value.manifest.title;
                UINTN k = 0u;
                while (left[k] != '\0' && left[k] == right[k]) ++k;
                if ((unsigned char)left[k] <= (unsigned char)right[k]) break;
                copy_bytes(&app->games[j], &app->games[j - 1u], sizeof(value));
                --j;
            }
            copy_bytes(&app->games[j], &value, sizeof(value));
        }
    }
    return EFI_ERROR(status) ? status : EFI_SUCCESS;
}

static uint8_t glyph_row(char value, UINTN row) {
    static const uint8_t letters[26][7] = {
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{15,16,16,16,16,16,15},
        {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {15,16,16,19,17,17,15},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
        {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
    };
    static const uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}
    };
    if (row >= 7u) return 0u;
    if (value >= 'a' && value <= 'z') value = (char)(value - 'a' + 'A');
    if (value >= 'A' && value <= 'Z') return letters[(unsigned)(value - 'A')][row];
    if (value >= '0' && value <= '9') return digits[(unsigned)(value - '0')][row];
    if (value == '-') return row == 3u ? 31u : 0u;
    if (value == '+') { static const uint8_t g[7]={0,4,4,31,4,4,0}; return g[row]; }
    if (value == '?') { static const uint8_t g[7]={14,17,1,2,4,0,4}; return g[row]; }
    if (value == '/') { static const uint8_t g[7]={1,2,2,4,8,8,16}; return g[row]; }
    if (value == '_') return row == 6u ? 31u : 0u;
    if (value == '.') return row == 6u ? 4u : 0u;
    if (value == ':') return (row == 2u || row == 5u) ? 4u : 0u;
    if (value == '>') { static const uint8_t g[7]={16,8,4,2,4,8,16}; return g[row]; }
    if (value == '<') { static const uint8_t g[7]={1,2,4,8,4,2,1}; return g[row]; }
    return 0u;
}

static void rectangle(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *pixels, UINTN width, UINTN height,
                      UINTN x, UINTN y, UINTN w, UINTN h,
                      EFI_GRAPHICS_OUTPUT_BLT_PIXEL color) {
    UINTN right = x + w > width ? width : x + w;
    UINTN bottom = y + h > height ? height : y + h;
    UINTN py, px;
    for (py = y; py < bottom; ++py)
        for (px = x; px < right; ++px) pixels[py * width + px] = color;
}

static void draw_text(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *pixels, UINTN width, UINTN height,
                      UINTN x, UINTN y, const char *text, UINTN scale,
                      EFI_GRAPHICS_OUTPUT_BLT_PIXEL color) {
    while (*text != '\0') {
        UINTN row, column;
        if (*text == '\n') { y += 9u * scale; x = 0u; ++text; continue; }
        for (row = 0u; row < 7u; ++row) {
            uint8_t bits = glyph_row(*text, row);
            for (column = 0u; column < 5u; ++column) {
                if ((bits & (1u << (4u - column))) != 0u)
                    rectangle(pixels, width, height, x + column * scale,
                              y + row * scale, scale, scale, color);
            }
        }
        x += 6u * scale;
        ++text;
    }
}

static void draw_text_limit(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *pixels, UINTN width,
                            UINTN height, UINTN x, UINTN y, const char *text,
                            UINTN scale, UINTN characters,
                            EFI_GRAPHICS_OUTPUT_BLT_PIXEL color) {
    char bounded[65];
    UINTN i = 0u;
    if (characters >= sizeof(bounded)) characters = sizeof(bounded) - 1u;
    while (i < characters && text[i] != '\0') { bounded[i] = text[i]; ++i; }
    bounded[i] = '\0';
    draw_text(pixels, width, height, x, y, bounded, scale, color);
}

static void draw_wrapped(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *pixels, UINTN width,
                         UINTN height, UINTN x, UINTN y, const char *text,
                         UINTN scale, UINTN columns, UINTN lines,
                         EFI_GRAPHICS_OUTPUT_BLT_PIXEL color) {
    char line[65];
    UINTN row = 0u, column = 0u;
    while (*text != '\0' && row < lines) {
        if (*text == '\n' || column == columns) {
            line[column] = '\0';
            draw_text(pixels, width, height, x, y + row * 18u, line, scale, color);
            ++row; column = 0u;
            if (*text == '\n') ++text;
            continue;
        }
        line[column++] = *text++;
    }
    if (column != 0u && row < lines) {
        line[column] = '\0';
        draw_text(pixels, width, height, x, y + row * 18u, line, scale, color);
    }
}

static void format_size(uint64_t bytes, char value[24]) {
    UINTN number;
    UINTN offset = 0u;
    const char *suffix;
    if (bytes >= 1024u * 1024u) { number = (UINTN)(bytes / (1024u * 1024u)); suffix = " MB"; }
    else if (bytes >= 1024u) { number = (UINTN)(bytes / 1024u); suffix = " KB"; }
    else { number = (UINTN)bytes; suffix = " BYTES"; }
    if (number >= 100u) value[offset++] = (char)('0' + (number / 100u) % 10u);
    if (number >= 10u) value[offset++] = (char)('0' + (number / 10u) % 10u);
    value[offset++] = (char)('0' + number % 10u);
    while (*suffix != '\0') value[offset++] = *suffix++;
    value[offset] = '\0';
}

static void dashboard_titles(const launcher *app, const char **titles) {
    UINTN index;
    for (index = 0u; index < app->game_count; ++index)
        titles[index] = app->games[index].manifest.title;
}

static void rebuild_dashboard(launcher *app) {
    const char *titles[MAX_GAMES];
    dashboard_titles(app, titles);
    oc_dashboard_rebuild(&app->dashboard, titles, app->game_count);
}

static UINTN ascii_length(const char *text) {
    UINTN length = 0u;
    while (text[length] != '\0') ++length;
    return length;
}

static void format_time(launcher *app, char value[10]) {
    EFI_TIME time;
    if (app->st->runtime_services == 0 ||
        app->st->runtime_services->get_time == 0 ||
        EFI_ERROR(app->st->runtime_services->get_time(&time, 0))) {
        oc_format_utc_time(value, 0, 0u, 0u, 0, 0u);
        return;
    }
    oc_format_utc_time(value, 1, time.hour, time.minute,
                       time.time_zone, time.daylight);
}

static void append_number(char *value, UINTN *offset, UINTN number) {
    if (number >= 10u) value[(*offset)++] = (char)('0' + number / 10u);
    value[(*offset)++] = (char)('0' + number % 10u);
}

static void format_result_range(const launcher *app, char value[20]) {
    UINTN count = app->dashboard.result_count;
    UINTN first = count == 0u ? 0u : app->dashboard.page * app->dashboard.page_capacity + 1u;
    UINTN last = count == 0u ? 0u : first + app->dashboard.visible_count - 1u;
    UINTN offset = 0u;
    append_number(value, &offset, first);
    value[offset++] = ' '; value[offset++] = '-'; value[offset++] = ' ';
    append_number(value, &offset, last);
    value[offset++] = ' '; value[offset++] = 'O'; value[offset++] = 'F'; value[offset++] = ' ';
    append_number(value, &offset, count);
    value[offset] = '\0';
}

static void format_page_count(const launcher *app, char value[15]) {
    UINTN page = app->dashboard.page_count == 0u ? 0u : app->dashboard.page + 1u;
    UINTN count = app->dashboard.page_count;
    UINTN offset = 0u;
    value[offset++] = 'P'; value[offset++] = 'A'; value[offset++] = 'G';
    value[offset++] = 'E'; value[offset++] = ' ';
    if (page >= 10u) value[offset++] = (char)('0' + page / 10u);
    value[offset++] = (char)('0' + page % 10u);
    value[offset++] = ' '; value[offset++] = 'O'; value[offset++] = 'F';
    value[offset++] = ' ';
    if (count >= 10u) value[offset++] = (char)('0' + count / 10u);
    value[offset++] = (char)('0' + count % 10u);
    value[offset] = '\0';
}

static void debug_dashboard_state(const launcher *app) {
    UINTN index;
    debug_write("OPENCONSOLE_DASHBOARD_QUERY_");
    if (app->dashboard.query_length == 0u) debug_write("EMPTY");
    else for (index = 0u; index < app->dashboard.query_length; ++index)
        debug_putc(app->dashboard.query[index]);
    debug_write("\nOPENCONSOLE_DASHBOARD_RESULTS_");
    debug_hex_uintn(app->dashboard.result_count);
    debug_write("OPENCONSOLE_DASHBOARD_PAGE_");
    debug_hex_uintn(app->dashboard.page + 1u);
    debug_write("OPENCONSOLE_DASHBOARD_PAGE_COUNT_");
    debug_hex_uintn(app->dashboard.page_count);
}

static void draw_cursor(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *pixels, UINTN width,
                        UINTN height, UINTN x, UINTN y) {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL black = {0, 0, 0, 0};
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL white = {255, 255, 255, 0};
    UINTN row;
    for (row = 0u; row < 18u; ++row) {
        UINTN span = row < 12u ? row / 2u + 2u : 4u;
        rectangle(pixels, width, height, x, y + row, span + 2u, 1u, black);
        if (span != 0u) rectangle(pixels, width, height, x + 1u, y + row,
                                  span, 1u, white);
    }
}

static int draw_jacket(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *pixels, UINTN screen_width,
                       UINTN screen_height, UINTN x, UINTN y, UINTN side,
                       const void *data, UINTN size) {
    const uint8_t *bytes = data;
    UINTN width, height, py, px;
    if (data == 0 || size < 8u || bytes[0] != 'O' || bytes[1] != 'C' ||
        bytes[2] != 'J' || bytes[3] != '1') return 0;
    width = (UINTN)bytes[4] | ((UINTN)bytes[5] << 8);
    height = (UINTN)bytes[6] | ((UINTN)bytes[7] << 8);
    if (width == 0u || height == 0u || width > 512u || height > 512u ||
        size != 8u + width * height * 4u) return 0;
    for (py = 0u; py < side && y + py < screen_height; ++py) {
        UINTN source_y = py * height / side;
        for (px = 0u; px < side && x + px < screen_width; ++px) {
            UINTN source_x = px * width / side;
            const uint8_t *source = bytes + 8u + (source_y * width + source_x) * 4u;
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL color = {source[0], source[1], source[2], 0u};
            pixels[(y + py) * screen_width + x + px] = color;
        }
    }
    return 1;
}

typedef oc_details_target modal_action;
#define MODAL_NONE OC_DETAILS_NONE
#define MODAL_CLOSE OC_DETAILS_CLOSE
#define MODAL_LAUNCH OC_DETAILS_LAUNCH
#define MODAL_DELETE OC_DETAILS_DELETE
#define MODAL_CANCEL OC_DETAILS_CANCEL
#define MODAL_CONFIRM OC_DETAILS_CONFIRM

static oc_dashboard_rect modal_action_rect(UINTN width, UINTN height,
                                            modal_action action) {
    oc_dashboard_rect rect = {0u, 0u, 0u, 0u};
    if (action == MODAL_CLOSE) rect = (oc_dashboard_rect){width - 150u, 126u, 42u, 36u};
    else if (action == MODAL_LAUNCH) rect = (oc_dashboard_rect){width - 410u, height - 128u, 120u, 42u};
    else if (action == MODAL_DELETE) rect = (oc_dashboard_rect){width - 270u, height - 128u, 120u, 42u};
    else if (action == MODAL_CANCEL) rect = (oc_dashboard_rect){width / 2u - 140u, height / 2u + 52u, 120u, 42u};
    else if (action == MODAL_CONFIRM) rect = (oc_dashboard_rect){width / 2u + 20u, height / 2u + 52u, 120u, 42u};
    return rect;
}

static int rect_contains(const oc_dashboard_rect *rect, UINTN x, UINTN y) {
    return rect->width != 0u && rect->height != 0u && x >= rect->x && y >= rect->y &&
           x - rect->x < rect->width && y - rect->y < rect->height;
}

static modal_action modal_hit(const launcher *app, UINTN width, UINTN height) {
    modal_action first = app->modal_delete_confirm ? MODAL_CANCEL : MODAL_CLOSE;
    modal_action last = app->modal_delete_confirm ? MODAL_CONFIRM : MODAL_DELETE;
    modal_action action;
    for (action = first; action <= last; action = (modal_action)(action + 1)) {
        oc_dashboard_rect rect = modal_action_rect(width, height, action);
        if (rect_contains(&rect, app->dashboard.cursor_x, app->dashboard.cursor_y)) return action;
    }
    return MODAL_NONE;
}

static void draw_button(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *pixels, UINTN width, UINTN height,
                        oc_dashboard_rect rect, const char *label,
                        EFI_GRAPHICS_OUTPUT_BLT_PIXEL fill,
                        EFI_GRAPHICS_OUTPUT_BLT_PIXEL text) {
    rectangle(pixels, width, height, rect.x, rect.y, rect.width, rect.height, fill);
    draw_text(pixels, width, height, rect.x + 14u, rect.y + 13u, label, 2u, text);
}

static void render_modal(launcher *app, EFI_GRAPHICS_OUTPUT_BLT_PIXEL *pixels,
                         UINTN width, UINTN height) {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL navy = {28,20,14,0}, panel = {53,39,27,0};
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL raised = {78,57,38,0}, cyan = {202,222,71,0};
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL white = {238,240,244,0}, muted = {165,173,187,0};
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL danger = {70,65,180,0};
    game_entry *game;
    char size_text[24];
    char requirement_text[32];
    UINTN modal_x = 92u, modal_y = 108u, modal_w = width - 184u, modal_h = height - 166u;
    UINTN jacket_side = 238u;
    if (!app->modal_open || app->modal_game >= app->game_count) return;
    game = &app->games[app->modal_game];
    rectangle(pixels, width, height, 36u, 100u, width - 72u, height - 130u, navy);
    rectangle(pixels, width, height, modal_x, modal_y, modal_w, modal_h, panel);
    rectangle(pixels, width, height, modal_x, modal_y, modal_w, 3u, cyan);
    if (app->modal_delete_confirm) {
        draw_text(pixels, width, height, width / 2u - 170u, height / 2u - 62u,
                  "DELETE THIS GAME?", 3u, white);
        draw_text_limit(pixels, width, height, width / 2u - 170u, height / 2u - 18u,
                        game->manifest.title, 2u, 28u, cyan);
        draw_text(pixels, width, height, width / 2u - 170u, height / 2u + 16u,
                  "THIS CANNOT BE UNDONE", 2u, muted);
        draw_button(pixels, width, height, modal_action_rect(width,height,MODAL_CANCEL),
                    "CANCEL", raised, white);
        draw_button(pixels, width, height, modal_action_rect(width,height,MODAL_CONFIRM),
                    "DELETE", danger, white);
        return;
    }
    draw_button(pixels, width, height, modal_action_rect(width,height,MODAL_CLOSE),
                "X", raised, white);
    draw_text_limit(pixels, width, height, 380u, 142u, game->manifest.title, 3u, 34u, white);
    draw_text(pixels, width, height, 382u, 180u, "BY", 2u, muted);
    draw_text_limit(pixels, width, height, 420u, 180u,
                    game->manifest.developer[0] ? game->manifest.developer : "UNKNOWN DEVELOPER",
                    2u, 40u, cyan);
    draw_text(pixels, width, height, 382u, 212u, "DESCRIPTION", 2u, muted);
    draw_wrapped(pixels, width, height, 382u, 238u,
                 game->manifest.description[0] ? game->manifest.description : "NO DESCRIPTION PROVIDED",
                 2u, 53u, 5u, white);
    if (!draw_jacket(pixels, width, height, 124u, 142u, jacket_side,
                     game->jacket, game->jacket_size))
        rectangle(pixels, width, height, 124u, 142u, jacket_side, jacket_side, raised);
    draw_text(pixels, width, height, 124u, 402u, "TECHNICAL", 2u, cyan);
    format_size(game->package_size, size_text);
    draw_text(pixels, width, height, 124u, 430u, "SIZE", 2u, muted);
    draw_text(pixels, width, height, 250u, 430u, size_text, 2u, white);
    draw_text(pixels, width, height, 124u, 458u, "ARCH", 2u, muted);
    draw_text(pixels, width, height, 250u, 458u, game->manifest.architecture, 2u, white);
    draw_text(pixels, width, height, 124u, 486u, "GRAPHICS", 2u, muted);
    draw_text(pixels, width, height, 250u, 486u,
              game->manifest.graphics_api == OC_GRAPHICS_GLES2 ? "OPENGL ES 2.0" : "UEFI GOP", 2u, white);
    draw_text(pixels, width, height, 124u, 514u, "GPU ACCEL", 2u, muted);
    draw_text(pixels, width, height, 250u, 514u,
              game->manifest.accel ? "REQUESTED - NOT AVAILABLE" : "NOT REQUIRED", 2u,
              game->manifest.accel ? danger : cyan);
    draw_text(pixels, width, height, 560u, 430u, "FIRMWARE REQUIREMENTS", 2u, cyan);
    requirement_text[0] = (char)('0' + (game->manifest.format_version[0] == '2' ?
        game->manifest.required_capability_count : game->manifest.required_protocol_count));
    requirement_text[1] = ' '; requirement_text[2] = 'D'; requirement_text[3] = 'E';
    requirement_text[4] = 'C'; requirement_text[5] = 'L'; requirement_text[6] = 'A';
    requirement_text[7] = 'R'; requirement_text[8] = 'E'; requirement_text[9] = 'D';
    requirement_text[10] = '\0';
    draw_text(pixels, width, height, 560u, 458u, requirement_text, 2u, white);
    draw_text(pixels, width, height, 560u, 486u,
              game->host_supported ? "HOST: SUPPORTED" : "HOST: UNSUPPORTED",
              2u, game->host_supported ? cyan : danger);
    draw_text(pixels, width, height, 560u, 514u,
              game->manifest.accel ?
                (game->hardware_accel_supported ? "HARDWARE GPU: AVAILABLE" :
                                                   "HARDWARE GPU: UNAVAILABLE") :
                "LAUNCH READY", 2u,
              game->manifest.accel && !game->hardware_accel_supported ? muted : cyan);
    draw_button(pixels, width, height, modal_action_rect(width,height,MODAL_LAUNCH),
                game->host_supported ? "LAUNCH" : "BLOCKED",
                game->host_supported ? cyan : raised,
                game->host_supported ? navy : muted);
    draw_button(pixels, width, height, modal_action_rect(width,height,MODAL_DELETE),
                "DELETE", danger, white);
}

static EFI_STATUS render_home(launcher *app) {
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = app->gop->mode->info;
    UINTN width = info->horizontal_resolution;
    UINTN height = info->vertical_resolution;
    UINTN pixel_count;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *pixels;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL navy = {28, 20, 14, 0};
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL panel = {53, 39, 27, 0};
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL raised = {78, 57, 38, 0};
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL cyan = {202, 222, 71, 0};
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL white = {238, 240, 244, 0};
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL muted = {165, 173, 187, 0};
    oc_dashboard_layout layout;
    char time_text[10];
    char version_text[] = "V" OPENCONSOLE_VERSION;
    char result_text[20];
    char page_text[15];
    UINTN i;
    EFI_STATUS status;
    if (width == 0u || height == 0u || width > (UINTN)-1 / height) return EFI_NOT_FOUND;
    pixel_count = width * height;
    status = app->bs->allocate_pool(EFI_LOADER_DATA, pixel_count * sizeof(*pixels),
                                    (void **)&pixels);
    if (EFI_ERROR(status)) return status;
    rectangle(pixels, width, height, 0, 0, width, height, navy);
    rectangle(pixels, width, height, 0, 0, width, 96u, panel);
    rectangle(pixels, width, height, 0, 94u, width, 2u, cyan);
    draw_text(pixels, width, height, 36u, 22u, "OPENCONSOLE", 4u, white);
    draw_text(pixels, width, height, 38u, 62u, "GAME LIBRARY", 2u, cyan);
    format_time(app, time_text);
    {
        const char *network_text = app->net_online ? "ONLINE" :
            (app->net_ready ? "NETWORK READY" : "OFFLINE");
        UINTN version_width = ascii_length(version_text) * 12u;
        UINTN network_width = ascii_length(network_text) * 12u;
        UINTN time_width = ascii_length(time_text) * 12u;
        UINTN right = width > 36u ? width - 36u : width;
        draw_text(pixels, width, height, right - version_width, 50u,
                  version_text, 2u, muted);
        right -= version_width + 30u;
        draw_text(pixels, width, height, right - network_width, 50u,
                  network_text, 2u, app->net_online ? cyan : (app->net_ready ? white : muted));
        right -= network_width + 30u;
        draw_text(pixels, width, height, right - time_width, 50u,
                  time_text, 2u, white);
    }
    rectangle(pixels, width, height, 36u, 118u, width > 72u ? width - 72u : 0u,
              50u, raised);
    rectangle(pixels, width, height, 36u, 166u, width > 72u ? width - 72u : 0u,
              2u, cyan);
    draw_text(pixels, width, height, 52u, 135u,
              app->dashboard.query_length == 0u ? "SEARCH GAMES" :
              app->dashboard.query, 2u,
              app->dashboard.query_length == 0u ? muted : white);
    if (!oc_dashboard_apply_layout(&app->dashboard, width, height, &layout)) {
        app->bs->free_pool(pixels);
        return EFI_UNSUPPORTED;
    }
    format_result_range(app, result_text);
    if (app->game_count == 0u) {
        draw_text(pixels, width, height, 64u, 270u,
                  "NO VALID GAMES FOUND", 3u, white);
    } else if (app->dashboard.result_count == 0u) {
        draw_text(pixels, width, height, 64u, 270u,
                  "NO GAMES MATCH YOUR SEARCH", 3u, white);
        draw_text(pixels, width, height, 66u, 310u,
                  "BACKSPACE TO EDIT   ESC TO CLEAR", 2u, muted);
    } else {
        format_page_count(app, page_text);
        if (app->dashboard.page > 0u) {
            rectangle(pixels, width, height, app->dashboard.previous_rect.x,
                      app->dashboard.previous_rect.y,
                      app->dashboard.previous_rect.width,
                      app->dashboard.previous_rect.height,
                      app->dashboard.hover.kind == OC_DASHBOARD_TARGET_PREVIOUS ? cyan : raised);
            draw_text(pixels, width, height, app->dashboard.previous_rect.x + 18u,
                      app->dashboard.previous_rect.y + 10u, "< PREV", 2u, navy);
        }
        if (app->dashboard.page + 1u < app->dashboard.page_count) {
            rectangle(pixels, width, height, app->dashboard.next_rect.x,
                      app->dashboard.next_rect.y,
                      app->dashboard.next_rect.width,
                      app->dashboard.next_rect.height,
                      app->dashboard.hover.kind == OC_DASHBOARD_TARGET_NEXT ? cyan : raised);
            draw_text(pixels, width, height, app->dashboard.next_rect.x + 18u,
                      app->dashboard.next_rect.y + 10u, "NEXT >", 2u, navy);
        }
        draw_text(pixels, width, height, width / 2u - 72u, 194u,
                  page_text, 2u, muted);
        draw_text(pixels, width, height,
                  width / 2u - ascii_length(result_text) * 6u,
                  218u, result_text, 2u, muted);
        for (i = 0u; i < app->dashboard.visible_count; ++i) {
            UINTN result_index = app->dashboard.page *
                app->dashboard.page_capacity + i;
            UINTN game_index = oc_dashboard_visible_game(&app->dashboard, i);
            UINTN column = i % layout.columns;
            UINTN row = i / layout.columns;
            UINTN x = layout.start_x + column * (layout.card_side + layout.gap);
            UINTN y = layout.card_y + row * (layout.card_side + layout.row_gap);
            int hovered = app->dashboard.hover.kind == OC_DASHBOARD_TARGET_CARD &&
                app->dashboard.hover.result_index == result_index;
            int pressed = hovered &&
                app->dashboard.pressed.kind == OC_DASHBOARD_TARGET_CARD &&
                app->dashboard.pressed.result_index == result_index;
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL border = pressed ? white :
                (hovered || result_index == app->dashboard.selected_result ? cyan : raised);
            rectangle(pixels, width, height, x - 6u, y - 6u,
                      layout.card_side + 12u, layout.card_side + 12u, border);
            if (!draw_jacket(pixels, width, height, x, y, layout.card_side,
                             app->games[game_index].jacket,
                             app->games[game_index].jacket_size)) {
                rectangle(pixels, width, height, x, y, layout.card_side,
                          layout.card_side, panel);
                draw_text(pixels, width, height, x + layout.card_side / 2u - 18u,
                          y + layout.card_side / 2u - 14u, "OC", 4u, cyan);
            }
            draw_text_limit(pixels, width, height, x, y + layout.card_side + 16u,
                      app->games[game_index].manifest.title, 2u, 9u,
                      (hovered || result_index == app->dashboard.selected_result) ? white : muted);
            rectangle(pixels, width, height, app->dashboard.info_rects[i].x,
                      app->dashboard.info_rects[i].y, 22u, 22u,
                      app->dashboard.hover.kind == OC_DASHBOARD_TARGET_INFO &&
                      app->dashboard.hover.result_index == result_index ? cyan : raised);
            draw_text(pixels, width, height, app->dashboard.info_rects[i].x + 7u,
                      app->dashboard.info_rects[i].y + 5u, "I", 2u, white);
        }
    }
    draw_text(pixels, width, height, 40u, height > 46u ? height - 42u : 0u,
              "TYPE TO SEARCH   CLICK OR ENTER PLAY   PAGE KEYS BROWSE", 2u, muted);
    if (app->status != 0)
        draw_text(pixels, width, height, 40u, height > 22u ? height - 20u : 0u,
                  app->status, 1u, cyan);
    if (app->modal_open) render_modal(app, pixels, width, height);
    if (app->dashboard.cursor_visible)
        draw_cursor(pixels, width, height, app->dashboard.cursor_x,
                    app->dashboard.cursor_y);
    status = app->gop->blt(app->gop, pixels,
                           EFI_GRAPHICS_OUTPUT_BLT_BUFFER_TO_VIDEO,
                           0u, 0u, 0u, 0u, width, height, 0u);
    app->bs->free_pool(pixels);
    return status;
}

static void reset_pointer(launcher *app) {
    EFI_SIMPLE_POINTER_STATE state;
    if (app->pointer == 0) return;
    if (EFI_ERROR(app->pointer->reset(app->pointer, 0u))) {
        app->pointer = 0;
        app->dashboard.cursor_visible = 0u;
        debug_write("OPENCONSOLE_POINTER_DISABLED\n");
        return;
    }
    while (!EFI_ERROR(app->pointer->get_state(app->pointer, &state))) { }
    app->dashboard.left_button = 0u;
    app->dashboard.pressed.kind = OC_DASHBOARD_TARGET_NONE;
    app->dashboard.pressed.result_index = OC_DASHBOARD_NO_INDEX;
    debug_write("OPENCONSOLE_POINTER_RESET_AFTER_CHILD\n");
}

static size_t drain_pointer(launcher *app, int *changed) {
    EFI_SIMPLE_POINTER_STATE state;
    EFI_STATUS status;
    size_t activated = OC_DASHBOARD_NO_INDEX;
    if (changed != 0) *changed = 0;
    if (app->pointer == 0) return activated;
    for (;;) {
        int page_changed = 0;
        size_t candidate;
        status = app->pointer->get_state(app->pointer, &state);
        if (status == EFI_NOT_READY) break;
        if (EFI_ERROR(status)) {
            app->pointer = 0;
            app->dashboard.cursor_visible = 0u;
            debug_write("OPENCONSOLE_POINTER_DISABLED\n");
            break;
        }
        if (changed != 0) *changed = 1;
        oc_dashboard_move_pointer(&app->dashboard, state.relative_movement_x,
            state.relative_movement_y, app->pointer->mode->resolution_x,
            app->pointer->mode->resolution_y,
            app->gop->mode->info->horizontal_resolution,
            app->gop->mode->info->vertical_resolution);
        if (app->modal_open) {
            modal_action current = modal_hit(app,
                app->gop->mode->info->horizontal_resolution,
                app->gop->mode->info->vertical_resolution);
            oc_details_input input = {app->modal_open, app->modal_delete_confirm,
                                      app->modal_pressed};
            oc_details_effect effect = OC_DETAILS_EFFECT_NONE;
            candidate = OC_DASHBOARD_NO_INDEX;
            if (state.left_button != app->dashboard.left_button)
                effect = oc_details_pointer(&input, current, state.left_button,
                    app->modal_game < app->game_count &&
                    app->games[app->modal_game].host_supported);
            app->modal_open = input.open;
            app->modal_delete_confirm = input.confirming_delete;
            app->modal_pressed = input.pressed;
            if (effect == OC_DETAILS_EFFECT_LAUNCH) app->modal_launch_requested = 1u;
            else if (effect == OC_DETAILS_EFFECT_DELETE) app->modal_delete_requested = 1u;
            app->dashboard.left_button = state.left_button ? 1u : 0u;
        } else {
            candidate = oc_dashboard_pointer_button(&app->dashboard,
                                                     state.left_button,
                                                     &page_changed);
            if (app->dashboard.details_game != OC_DASHBOARD_NO_INDEX) {
                app->modal_game = app->dashboard.details_game;
                app->dashboard.details_game = OC_DASHBOARD_NO_INDEX;
                app->modal_open = 1u;
                app->modal_delete_confirm = 0u;
                debug_write("OPENCONSOLE_DETAILS_OPEN_ID_");
                debug_write(app->games[app->modal_game].manifest.id);
                debug_write("\n");
            }
        }
        debug_write("OPENCONSOLE_POINTER_CURSOR_X_");
        debug_hex_uintn(app->dashboard.cursor_x);
        debug_write("OPENCONSOLE_POINTER_CURSOR_Y_");
        debug_hex_uintn(app->dashboard.cursor_y);
        if (!app->modal_open && app->dashboard.hover.kind == OC_DASHBOARD_TARGET_CARD &&
            app->dashboard.hover.result_index < app->dashboard.result_count) {
            size_t game = app->dashboard.result_indices[
                app->dashboard.hover.result_index];
            debug_write("OPENCONSOLE_POINTER_HOVER_ID_");
            debug_write(app->games[game].manifest.id);
            debug_write("\n");
        }
        if (page_changed) {
            oc_dashboard_layout layout;
            (void)oc_dashboard_apply_layout(&app->dashboard,
                app->gop->mode->info->horizontal_resolution,
                app->gop->mode->info->vertical_resolution, &layout);
            debug_write("OPENCONSOLE_POINTER_PAGE_CHANGED_");
            debug_hex_uintn(app->dashboard.page + 1u);
        }
        if (candidate != OC_DASHBOARD_NO_INDEX) {
            activated = candidate;
            debug_write("OPENCONSOLE_POINTER_ACTIVATED_INDEX_");
            debug_hex_uintn(candidate);
        }
    }
    return activated;
}

#if !defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
static int safe_directory_component(const CHAR16 *name) {
    UINTN i = 0u;
    if (name == 0 || name[0] == 0u || (name[0] == '.' && name[1] == 0u) ||
        (name[0] == '.' && name[1] == '.' && name[2] == 0u)) return 0;
    while (name[i] != 0u) {
        if (++i >= 128u || name[i - 1u] == '/' || name[i - 1u] == '\\' ||
            name[i - 1u] < 0x20u) return 0;
    }
    return 1;
}
#endif

static int directory_matches_id(const CHAR16 *directory, const char *id) {
    UINTN i = 0u;
    while (directory[i] != 0u && id[i] != '\0') {
        if (directory[i] != (CHAR16)(uint8_t)id[i]) return 0;
        ++i;
    }
    return directory[i] == 0u && id[i] == '\0' && oc_delete_component_valid(id);
}

#if !defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
static EFI_STATUS delete_tree(launcher *app, EFI_FILE_PROTOCOL *directory,
                              UINTN depth, UINTN *deleted) {
    static const CHAR16 dot[] = L".";
    static const CHAR16 dotdot[] = L"..";
    EFI_FILE_INFO *info = 0;
    EFI_STATUS status;
    if (depth > 8u || *deleted > 1024u) return EFI_SECURITY_VIOLATION;
    status = app->bs->allocate_pool(EFI_LOADER_DATA, 2048u, (void **)&info);
    if (EFI_ERROR(status)) return status;
    for (;;) {
        UINTN size = 2048u;
        EFI_FILE_PROTOCOL *child = 0;
        status = directory->set_position(directory, 0u);
        if (EFI_ERROR(status)) break;
        do {
            size = 2048u;
            status = directory->read(directory, &size, info);
            if (EFI_ERROR(status) || size == 0u) break;
        } while (char16_equal(info->file_name, dot) || char16_equal(info->file_name, dotdot));
        if (EFI_ERROR(status) || size == 0u) break;
        if (!safe_directory_component(info->file_name) || ++*deleted > 1024u) {
            status = EFI_SECURITY_VIOLATION;
            break;
        }
        status = directory->open(directory, &child, info->file_name,
                                 EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0u);
        if (EFI_ERROR(status)) break;
        if ((info->attribute & EFI_FILE_DIRECTORY) != 0u)
            status = delete_tree(app, child, depth + 1u, deleted);
        else
            status = child->delete_file(child);
        if (EFI_ERROR(status)) break;
    }
    app->bs->free_pool(info);
    if (EFI_ERROR(status)) { directory->close(directory); return status; }
    return directory->delete_file(directory);
}
#endif

static void release_discovered_games(launcher *app) {
    UINTN i;
    for (i = 0u; i < app->game_count; ++i) {
        if (app->games[i].jacket != 0) app->bs->free_pool(app->games[i].jacket);
        zero_bytes(&app->games[i], sizeof(app->games[i]));
    }
    app->game_count = 0u;
}

static EFI_STATUS delete_modal_game(launcher *app) {
    if (app->data_volume_ambiguous) return EFI_SECURITY_VIOLATION;
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    EFI_STATUS status;
    game_entry *game;
    oc_game_selection committed;
    size_t deleted = 0u;
    oc_game_storage_result result;
    if (!app->modal_open || app->modal_game >= app->game_count ||
        !app->game_storage_ready) return EFI_SECURITY_VIOLATION;
    game = &app->games[app->modal_game];
    if (!game->transactional ||
        !directory_matches_id(game->directory, game->manifest.id))
        return EFI_SECURITY_VIOLATION;
    debug_write("OPENCONSOLE_DELETE_CONFIRMED_ID_");
    debug_write(game->manifest.id); debug_write("\n");
    result = oc_game_tombstone_and_cleanup(&app->game_storage.operations,
        game->manifest.id, 1, OC_GAME_GC_MAX_DELETES, &deleted, &committed);
    status = result == OC_GAME_STORAGE_OK || result == OC_GAME_STORAGE_CLEANUP ?
        EFI_SUCCESS : EFI_DEVICE_ERROR;
    if (!EFI_ERROR(status)) {
        debug_write("OPENCONSOLE_DELETE_TOMBSTONE_COMMITTED_ID_");
        debug_write(game->manifest.id); debug_write("\n");
    }
    release_discovered_games(app);
    (void)discover_games(app);
    rebuild_dashboard(app);
    debug_dashboard_state(app);
    app->modal_open = 0u;
    app->modal_delete_confirm = 0u;
    return status;
#else
    static const CHAR16 games_name[] = L"games";
    static const CHAR16 manifest_name[] = L"game.toml";
    EFI_FILE_PROTOCOL *games_dir = 0, *game_dir = 0;
    void *manifest = 0;
    UINTN manifest_size = 0u, deleted = 0u;
    EFI_STATUS status;
    game_entry *game;
    if (!app->modal_open || app->modal_game >= app->game_count)
        return EFI_SECURITY_VIOLATION;
    game = &app->games[app->modal_game];
    if (!safe_directory_component(game->directory) ||
        !directory_matches_id(game->directory, game->manifest.id))
        return EFI_SECURITY_VIOLATION;
    status = app->root->open(app->root, &games_dir, games_name,
                             EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0u);
    if (EFI_ERROR(status)) return status;
    status = games_dir->open(games_dir, &game_dir, game->directory,
                             EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0u);
    if (EFI_ERROR(status)) goto done;
    status = read_file(app, game_dir, manifest_name, OC_MANIFEST_MAX_BYTES,
                       &manifest, &manifest_size);
    if (EFI_ERROR(status) || hash_bytes(manifest, manifest_size) != game->manifest_hash) {
        if (!EFI_ERROR(status)) status = EFI_SECURITY_VIOLATION;
        goto done;
    }
    app->bs->free_pool(manifest); manifest = 0;
    debug_write("OPENCONSOLE_DELETE_CONFIRMED_ID_");
    debug_write(game->manifest.id); debug_write("\n");
    status = delete_tree(app, game_dir, 0u, &deleted);
    game_dir = 0;
    if (EFI_ERROR(status)) goto done;
    status = games_dir->flush(games_dir);
    if (EFI_ERROR(status)) goto done;
    debug_write("OPENCONSOLE_DELETE_SUCCESS_ID_");
    debug_write(game->manifest.id); debug_write("\n");
done:
    if (manifest != 0) app->bs->free_pool(manifest);
    if (game_dir != 0) game_dir->close(game_dir);
    if (games_dir != 0) games_dir->close(games_dir);
    release_discovered_games(app);
    (void)discover_games(app);
    rebuild_dashboard(app);
    debug_dashboard_state(app);
    app->modal_open = 0u;
    app->modal_delete_confirm = 0u;
    return status;
#endif
}

static EFI_STATUS launch_selected(launcher *app) {
    UINTN selected = app->modal_launch_requested ? app->modal_game :
        oc_dashboard_selected_game(&app->dashboard);
    game_entry *game;
#if !defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    EFI_FILE_PROTOCOL *games_dir = 0;
    EFI_FILE_PROTOCOL *game_dir = 0;
    static const CHAR16 games_name[] = L"games";
#endif
    CHAR16 entry_path[257];
    void *image_data = 0;
    void *child_path = 0;
    UINTN image_size = 0u;
    EFI_HANDLE child = 0;
    UINTN exit_data_size = 0u;
    CHAR16 *exit_data = 0;
    EFI_STATUS status;
    int image_started = 0;
    if (selected == (UINTN)-1 || selected >= app->game_count) return EFI_NOT_FOUND;
    game = &app->games[selected];
    if (game->quarantined) {
        debug_write("OPENCONSOLE_LAUNCH_REJECTED_QUARANTINED\n");
        return EFI_SECURITY_VIOLATION;
    }
    debug_write("OPENCONSOLE_DASHBOARD_LAUNCH_INDEX_");
    debug_hex_uintn(selected);
    debug_write("OPENCONSOLE_DASHBOARD_LAUNCH_ID_");
    debug_write(game->manifest.id);
    debug_write("\n");
 #if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    {
        oc_game_selection current;
        oc_game_storage_result recovered;
        size_t cleanup_deletes = 0u, i;
        char path[OC_GAME_STORAGE_PATH_MAX];
        if (!game->transactional || !app->game_storage_ready)
            return EFI_SECURITY_VIOLATION;
        recovered = oc_game_transaction_recover(&app->game_storage.operations,
            game->manifest.id, OC_GAME_GC_MAX_DELETES, &cleanup_deletes, &current);
        if (recovered != OC_GAME_STORAGE_OK || current.tombstone ||
            current.generation != game->selection.generation ||
            current.slot != game->selection.slot) return EFI_SECURITY_VIOLATION;
        for (i = 0u; i < 32u; ++i)
            if (current.bundle_id[i] != game->selection.bundle_id[i])
                return EFI_SECURITY_VIOLATION;
        if (oc_game_package_path(game->manifest.id, current.slot,
                game->manifest.entry, path, sizeof(path)) != OC_GAME_STORAGE_OK ||
            !ascii_path_to_char16(entry_path,
                sizeof(entry_path) / sizeof(entry_path[0]), path))
            return EFI_SECURITY_VIOLATION;
#if defined(OPENCONSOLE_TEST_MUTATE_GAME_BEFORE_LOAD)
        {
            static const uint8_t corrupt_bundle[] = {0x00u};
            char bundle_path[OC_GAME_STORAGE_PATH_MAX];
            if (oc_game_storage_path(game->manifest.id, OC_GAME_PATH_BUNDLE,
                    current.slot, bundle_path, sizeof(bundle_path)) != OC_GAME_STORAGE_OK ||
                !app->game_storage.operations.write_file(
                    app->game_storage.operations.context, bundle_path,
                    corrupt_bundle, sizeof(corrupt_bundle)) ||
                !app->game_storage.operations.flush_file(
                    app->game_storage.operations.context, bundle_path))
                return EFI_DEVICE_ERROR;
            debug_write("OPENCONSOLE_TEST_GAME_MUTATED_AFTER_DISCOVERY\n");
        }
#endif
        status = oc_uefi_game_storage_read_authenticated_image(&app->game_storage,
            &current, &image_data, &image_size);
        if (EFI_ERROR(status)) {
            debug_write("OPENCONSOLE_INTEGRITY_GAME_REOPEN_REJECTED\n");
            return status;
        }
        status = make_file_device_path(app, entry_path, &child_path);
        if (EFI_ERROR(status)) { app->bs->free_pool(image_data); return status; }
        debug_write("OPENCONSOLE_INTEGRITY_GAME_REOPEN_VERIFIED\n");
    }
#else
    if (!ascii_path_to_char16(entry_path, sizeof(entry_path) / sizeof(entry_path[0]),
                              game->manifest.entry)) return EFI_NOT_FOUND;
    status = app->root->open(app->root, &games_dir, games_name, EFI_FILE_MODE_READ, 0u);
    if (EFI_ERROR(status)) return status;
    status = games_dir->open(games_dir, &game_dir, game->directory, EFI_FILE_MODE_READ, 0u);
    if (EFI_ERROR(status)) goto done;
    status = read_file(app, game_dir, entry_path, MAX_FILE_BYTES, &image_data, &image_size);
    if (EFI_ERROR(status)) goto done;
#endif
    debug_write("OPENCONSOLE_CHILD_STARTING\n");
    status = app->bs->load_image(0u, app->image, child_path, image_data, image_size, &child);
    if (EFI_ERROR(status)) goto done;
#if OPENCONSOLE_QEMU_TEST && !OPENCONSOLE_REAL_NET_TEST && !OPENCONSOLE_REAL_AUDIO_TEST && !OPENCONSOLE_INTEGRATED_IO_TEST
    ++app->child_generation;
    status = oc_io_mock_begin_generation(&app->io_mock, app->child_generation);
    if (EFI_ERROR(status)) goto done;
#endif
    if (app->net_ready) {
        ++app->net_generation;
        status = oc_net_provider_begin_generation(app->net_provider, app->net_generation);
        if (EFI_ERROR(status)) goto done;
    }
    if (app->audio_ready) {
        ++app->audio_generation;
        status = oc_audio_provider_begin_generation(app->audio_provider,
                                                    app->audio_generation);
        if (EFI_ERROR(status)) goto done;
    }
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    status = record_launch_state(app, game, OC_LAUNCH_ATTEMPT);
    if (EFI_ERROR(status)) goto done;
    debug_write("OPENCONSOLE_LAUNCH_ATTEMPT_PERSISTED\n");
    if (app->bs->set_watchdog_timer != 0) {
#if defined(OPENCONSOLE_TEST_WATCHDOG_UNSUPPORTED)
        status = EFI_UNSUPPORTED;
#else
        status = ((oc_set_watchdog_timer)app->bs->set_watchdog_timer)(300u,0x4f434f4eu,0u,0);
#endif
        if (EFI_ERROR(status)) debug_write("OPENCONSOLE_WATCHDOG_UNSUPPORTED\n");
        else debug_write("OPENCONSOLE_WATCHDOG_ARMED\n");
        status = EFI_SUCCESS;
    }
#endif
    status = app->bs->start_image(child, &exit_data_size, &exit_data);
    image_started = 1;
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    if (app->bs->set_watchdog_timer != 0)
        (void)((oc_set_watchdog_timer)app->bs->set_watchdog_timer)(0u,0u,0u,0);
    if (EFI_ERROR(record_launch_state(app, game, OC_LAUNCH_COMPLETE))) {
        debug_write("OPENCONSOLE_LAUNCH_COMPLETION_PERSIST_FAILED\n");
        status = EFI_DEVICE_ERROR;
    } else debug_write("OPENCONSOLE_LAUNCH_COMPLETION_PERSISTED\n");
#endif
    if (exit_data != 0) app->bs->free_pool(exit_data);
    reset_pointer(app);
#if OPENCONSOLE_QEMU_TEST && !OPENCONSOLE_REAL_NET_TEST && !OPENCONSOLE_REAL_AUDIO_TEST && !OPENCONSOLE_INTEGRATED_IO_TEST
    if (oc_io_mock_active_resources(&app->io_mock, app->child_generation) != 0u)
        debug_write("OPENCONSOLE_IO_PARENT_RECLAIMING\n");
    oc_io_mock_cleanup_generation(&app->io_mock, app->child_generation);
    if (oc_io_mock_active_resources(&app->io_mock, app->child_generation) == 0u)
        debug_write("OPENCONSOLE_IO_PARENT_CLEAN\n");
#endif
    if (app->net_ready) {
        oc_net_provider_cleanup_generation(app->net_provider, app->net_generation);
        if (oc_net_provider_active_resources(app->net_provider, app->net_generation) == 0u)
            debug_write("OPENCONSOLE_NET_GENERATION_CLEAN\n");
        debug_write("OPENCONSOLE_NET_ALLOC_PEAK_BYTES_");
        debug_hex_uintn(app->net_peak_allocation_bytes);
        debug_write("OPENCONSOLE_NET_ALLOC_LIVE_BYTES_");
        debug_hex_uintn(app->net_allocation_bytes);
        debug_write("OPENCONSOLE_NET_ALLOC_LIVE_COUNT_");
        debug_hex_uintn(app->net_allocation_count);
    }
    if (app->audio_ready) {
        if (oc_audio_provider_active_voices(app->audio_provider,
                                            app->audio_generation) != 0u)
            debug_write("OPENCONSOLE_AUDIO_PARENT_RECLAIMING\n");
        oc_audio_provider_cleanup_generation(app->audio_provider,
                                             app->audio_generation);
        (void)oc_audioio_backend_stop(app->audio_backend);
        if (oc_audio_provider_active_voices(app->audio_provider,
                                            app->audio_generation) == 0u &&
            oc_audioio_backend_pending_buffers(app->audio_backend) == 0u)
            debug_write("OPENCONSOLE_AUDIO_PARENT_CLEAN\n");
    }
done:
    if (image_data != 0) app->bs->free_pool(image_data);
    if (child_path != 0) app->bs->free_pool(child_path);
#if !defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    if (game_dir != 0) game_dir->close(game_dir);
    if (games_dir != 0) games_dir->close(games_dir);
#endif
    if (image_started) {
        EFI_STATUS child_status = status;
        release_discovered_games(app);
        if (!EFI_ERROR(discover_games(app))) {
            rebuild_dashboard(app);
            debug_dashboard_state(app);
            debug_write("OPENCONSOLE_LIBRARY_RESCANNED_AFTER_CHILD\n");
        }
        status = child_status;
    }
    return status;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    launcher app;
    EFI_STATUS status;
    zero_bytes(&app, sizeof(app));
    app.image = image_handle;
    app.st = system_table;
    app.bs = system_table->boot_services;
    debug_write("OPENCONSOLE_PARENT_BOOTED\n");
    status = open_boot_volume(&app);
    if (EFI_ERROR(status)) return status;
#if defined(OPENCONSOLE_DATA_VOLUME_TEST) && !defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    status = select_data_volume(&app);
    if (EFI_ERROR(status)) return status;
#endif
    status = ensure_update_crypto(&app);
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
    if (EFI_ERROR(status)) return status;
    status = load_platform_inventory(&app);
    if (EFI_ERROR(status)) return status;
    status = load_publisher_keys(&app);
    if (EFI_ERROR(status)) return status;
    status = select_data_volume(&app);
    if (EFI_ERROR(status)) return status;
    if (!app.data_volume_ambiguous) {
        status = oc_uefi_game_storage_initialize(&app.game_storage, app.bs, app.root,
            app.crypto_protocol, &app.publisher_keys);
        if (EFI_ERROR(status)) return status;
        app.game_storage_ready = 1u;
        debug_write("OPENCONSOLE_GAME_STORAGE_READY\n");
        status = load_launch_recovery(&app);
        if (EFI_ERROR(status)) return status;
    }
#else
    (void)status;
#endif
    install_fixture_tls_ca(&app);
    status = app.bs->locate_protocol(&gop_guid, 0, (void **)&app.gop);
    if (EFI_ERROR(status)) return status;
    (void)ensure_simple_pointer_driver(&app);
#if OPENCONSOLE_SLICE_TEST
    (void)select_display_mode(&app, 1920u, 1080u);
#endif
    {
        EFI_HANDLE *pointer_handles = 0;
        UINTN pointer_count = 0u;
        UINTN pointer_index;
        status = app.bs->locate_handle_buffer(EFI_LOCATE_BY_PROTOCOL,
            &simple_pointer_guid, 0, &pointer_count, &pointer_handles);
        if (!EFI_ERROR(status)) {
            for (pointer_index = 0u; pointer_index < pointer_count; ++pointer_index) {
                EFI_SIMPLE_POINTER_PROTOCOL *candidate = 0;
                if (!EFI_ERROR(app.bs->handle_protocol(pointer_handles[pointer_index],
                        &simple_pointer_guid, (void **)&candidate)) && candidate != 0 &&
                    candidate->reset != 0 && candidate->get_state != 0 &&
                    candidate->wait_for_input != 0 && candidate->mode != 0 &&
                    candidate->mode->left_button &&
                    !EFI_ERROR(candidate->reset(candidate, 0u))) {
                    debug_write("OPENCONSOLE_POINTER_CANDIDATE_RESOLUTION_X_");
                    debug_hex_uintn(candidate->mode->resolution_x);
                    app.pointer = candidate;
                }
            }
            app.bs->free_pool(pointer_handles);
        }
    }
    if (app.pointer != 0) {
        app.dashboard.cursor_x = app.gop->mode->info->horizontal_resolution / 2u;
        app.dashboard.cursor_y = app.gop->mode->info->vertical_resolution / 2u;
        app.dashboard.cursor_visible = 1u;
        if (EFI_ERROR(app.bs->create_event(EFI_EVENT_TIMER, EFI_TPL_APPLICATION,
                0, 0, &app.pointer_watchdog)) ||
            EFI_ERROR(app.bs->set_timer(app.pointer_watchdog,
                EFI_TIMER_PERIODIC, 160000u)))
            app.pointer_watchdog = 0;
        else
            debug_write("OPENCONSOLE_POINTER_WATCHDOG_READY\n");
        debug_write("OPENCONSOLE_POINTER_READY\n");
    } else {
        debug_write("OPENCONSOLE_POINTER_ABSENT_KEYBOARD_FALLBACK\n");
    }
    status = install_gles_provider(&app);
    if (EFI_ERROR(status)) return status;
#if OPENCONSOLE_QEMU_TEST && !OPENCONSOLE_REAL_NET_TEST && !OPENCONSOLE_REAL_AUDIO_TEST && !OPENCONSOLE_INTEGRATED_IO_TEST
    status = install_io_mock_providers(&app);
    if (EFI_ERROR(status)) return status;
#endif
    report_amd_pci(&app);
    {
        oc_io_inventory_result inventory;
        static const CHAR16 firmware_inventory_path[] =
            L"\\OPENCONSOLE-INVENTORY.LOG";
        open_inventory_log(&app, firmware_inventory_path);
        (void)oc_io_inventory_collect(app.bs, inventory_debug_write, &app, &inventory);
        close_inventory_log(&app);
    }
#if !OPENCONSOLE_QEMU_TEST || OPENCONSOLE_REAL_NET_TEST || OPENCONSOLE_INTEGRATED_IO_TEST
    install_network_provider(&app);
    probe_online(&app);
#endif
#if !OPENCONSOLE_QEMU_TEST || OPENCONSOLE_REAL_NET_TEST || OPENCONSOLE_REAL_AUDIO_TEST || OPENCONSOLE_INTEGRATED_IO_TEST
    if (!app.safe_mode) install_audio_provider(&app);
    else debug_write("OPENCONSOLE_SAFE_MODE_OPTIONAL_DRIVERS_SUPPRESSED\n");
#endif
#if OPENCONSOLE_PHYSICAL_TEST
    {
        static const CHAR16 driver_inventory_path[] =
            L"\\OPENCONSOLE-DRIVER-INVENTORY.LOG";
        oc_io_inventory_result inventory;
        open_inventory_log(&app, driver_inventory_path);
        (void)oc_io_inventory_collect(app.bs, inventory_debug_write, &app, &inventory);
        close_inventory_log(&app);
        debug_write("OPENCONSOLE_PHYSICAL_INVENTORIES_CAPTURED\n");
    }
#endif
    discover_games(&app);
    rebuild_dashboard(&app);
    app.status = app.safe_mode ? "SAFE MODE - PRESS R TO RETRY" : "READY";
    render_home(&app);
    debug_write("OPENCONSOLE_DASHBOARD_HEADER_READY\n");
    debug_dashboard_state(&app);
    debug_write("OPENCONSOLE_MENU_READY\n");
#if OPENCONSOLE_QEMU_TEST && OPENCONSOLE_BOOTNEXT_TEST
    debug_write("OPENCONSOLE_BOOTNEXT_PARENT_REACHED\n");
    qemu_exit(0x10);
#endif
    for (;;) {
        UINTN index = 0u;
        EFI_EVENT events[3];
        EFI_INPUT_KEY key;
        UINTN event_count = 1u;
        int launch = 0;
        int pointer_changed = 0;
        events[0] = system_table->con_in->wait_for_key;
        if (app.pointer != 0) events[event_count++] = app.pointer->wait_for_input;
        if (app.pointer_watchdog != 0) events[event_count++] = app.pointer_watchdog;
        status = app.bs->wait_for_event(event_count, events, &index);
        if (EFI_ERROR(status)) continue;
#if OPENCONSOLE_QEMU_TEST
        if (app.pointer != 0 && app.pointer_wait_events < 3u) {
            debug_write("OPENCONSOLE_POINTER_WAIT_EVENT_");
            debug_hex_uintn(index);
            ++app.pointer_wait_events;
        }
#endif
        if (app.pointer != 0 &&
            drain_pointer(&app, &pointer_changed) != OC_DASHBOARD_NO_INDEX)
            launch = 1;
        if (app.modal_launch_requested) launch = 1;
        if (app.modal_delete_requested) {
            app.modal_delete_requested = 0u;
            status = delete_modal_game(&app);
            app.status = EFI_ERROR(status) ? "DELETE FAILED - LIBRARY RESCANNED" :
                                             "GAME DELETED";
            render_home(&app);
            continue;
        }
        if (index != 0u && !pointer_changed && !launch) continue;
        if (index == 0u && !launch) {
            status = system_table->con_in->read_key_stroke(system_table->con_in,
                                                            &key);
            if (EFI_ERROR(status)) continue;
            if (key.unicode_char == 0u && key.scan_code == 23u)
                key.unicode_char = 27u;
#if defined(OPENCONSOLE_PINNED_PLATFORM_TRUST)
            if (app.safe_mode && (key.unicode_char == 'r' || key.unicode_char == 'R')) {
                status = clear_launch_quarantine(&app);
                if (!EFI_ERROR(status)) {
                    app.safe_mode = 0u;
                    release_discovered_games(&app);
                    (void)discover_games(&app);
                    rebuild_dashboard(&app);
                    app.status = "QUARANTINE CLEARED - SELECT GAME TO RETRY";
                    debug_write("OPENCONSOLE_RECOVERY_MANUAL_RETRY_ENABLED\n");
                    debug_dashboard_state(&app);
                    render_home(&app);
                }
                continue;
            }
#endif
            if (app.modal_open) {
                oc_details_input input = {app.modal_open, app.modal_delete_confirm,
                                          app.modal_pressed};
                oc_details_effect effect = oc_details_key(&input, key.unicode_char,
                    app.modal_game < app.game_count &&
                    app.games[app.modal_game].host_supported);
                app.modal_open = input.open;
                app.modal_delete_confirm = input.confirming_delete;
                app.modal_pressed = input.pressed;
                if (effect == OC_DETAILS_EFFECT_DELETE)
                    app.modal_delete_requested = 1u;
                else if (effect == OC_DETAILS_EFFECT_LAUNCH) {
                    app.modal_launch_requested = 1u;
                    launch = 1;
                }
            } else if ((key.unicode_char == 'i' || key.unicode_char == 'I') &&
                       app.dashboard.result_count != 0u) {
                app.modal_game = oc_dashboard_selected_game(&app.dashboard);
                app.modal_open = 1u;
                app.modal_delete_confirm = 0u;
                debug_write("OPENCONSOLE_DETAILS_OPEN_ID_");
                debug_write(app.games[app.modal_game].manifest.id);
                debug_write("\n");
            } else if ((key.scan_code == 1u || key.scan_code == 4u) &&
                app.dashboard.result_count != 0u) {
                oc_dashboard_move(&app.dashboard, -1);
                debug_write("OPENCONSOLE_SELECTION_CHANGED\n");
            } else if ((key.scan_code == 2u || key.scan_code == 3u) &&
                       app.dashboard.result_count != 0u) {
                oc_dashboard_move(&app.dashboard, 1);
                debug_write("OPENCONSOLE_SELECTION_CHANGED\n");
            } else if (key.scan_code == 9u) {
                if (oc_dashboard_change_page(&app.dashboard, -1))
                    debug_write("OPENCONSOLE_KEYBOARD_PAGE_PREVIOUS\n");
            } else if (key.scan_code == 10u) {
                if (oc_dashboard_change_page(&app.dashboard, 1))
                    debug_write("OPENCONSOLE_KEYBOARD_PAGE_NEXT\n");
            } else if (key.unicode_char == 13u &&
                       app.dashboard.result_count != 0u) {
                launch = 1;
            } else {
                const char *titles[MAX_GAMES];
                dashboard_titles(&app, titles);
                if (oc_dashboard_edit(&app.dashboard, key.unicode_char,
                                      titles, app.game_count)) {
                    app.status = app.dashboard.result_count == 0u ?
                        "NO MATCHES" : "SEARCH UPDATED";
                    debug_write("OPENCONSOLE_DASHBOARD_FILTER_RECONCILED\n");
                    debug_dashboard_state(&app);
                }
            }
        }
        if (launch) {
            app.status = "STARTING GAME";
            render_home(&app);
            status = launch_selected(&app);
            app.modal_launch_requested = 0u;
            app.status = EFI_ERROR(status) ? "GAME LOAD FAILED" : "GAME RETURNED SUCCESSFULLY";
            render_home(&app);
#if OPENCONSOLE_QEMU_TEST && OPENCONSOLE_INTEGRATED_IO_TEST
            if (EFI_ERROR(status)) debug_write("OPENCONSOLE_CHILD_ERROR_RETURNED\n");
            else debug_write("OPENCONSOLE_CHILD_RETURNED\n");
            ++app.successful_child_returns;
#if !OPENCONSOLE_SLICE_TEST
            if (app.successful_child_returns >= 2u) qemu_exit(0x10);
#endif
#else
            if (!EFI_ERROR(status)) {
                debug_write("OPENCONSOLE_CHILD_RETURNED\n");
#if OPENCONSOLE_QEMU_TEST
                ++app.successful_child_returns;
#if OPENCONSOLE_QEMU_REPEAT_TEST
                if (app.successful_child_returns >= 2u) qemu_exit(0x10);
#else
                qemu_exit(0x10);
#endif
#endif
            }
#endif
        }
        render_home(&app);
    }
}
