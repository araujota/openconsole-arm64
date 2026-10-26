#include "openconsole/net_protocol.h"

#define CONFIG_CAPACITY 2048u
#define EVIDENCE_CAPACITY 16384u
#define EFI_FILE_MODE_READ 1u
#define EFI_FILE_MODE_WRITE 2u

static const EFI_GUID loaded_image_guid =
    {0x5b1b31a1u,0x9562u,0x11d2u,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const EFI_GUID simple_fs_guid =
    {0x964e5b22u,0x6459u,0x11d2u,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_FILE_PROTOCOL *evidence_file;

typedef struct {
    char *dns_name;
    char *expected_dns_ipv4;
    char *endpoint_ipv4;
    char *udp_port;
    char *udp_request;
    char *udp_response;
    char *https_url;
    char *https_response;
} physical_config;

static void debug_putc(char value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"((uint16_t)0x402));
}

static void debug_write(const char *text) {
    while (*text != '\0') debug_putc(*text++);
}

static void evidence(const char *text) {
    UINTN size = 0u, written;
    while (text[size] != '\0') { debug_putc(text[size]); ++size; }
    if (evidence_file == 0 || size == 0u) return;
    written = size;
    if (EFI_ERROR(evidence_file->write(evidence_file, &written, text)) || written != size)
        evidence_file = 0;
}

static void evidence_hex32(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char text[10];
    UINTN index;
    for (index = 0u; index < 8u; ++index)
        text[index] = digits[(value >> (28u - index * 4u)) & 15u];
    text[8] = '\n'; text[9] = '\0'; evidence(text);
}

static int bytes_equal(const void *left, const void *right, UINTN size) {
    const uint8_t *a = left, *b = right;
    while (size-- != 0u) if (*a++ != *b++) return 0;
    return 1;
}

static UINTN text_size(const char *text, UINTN limit) {
    UINTN size = 0u;
    while (size < limit && text[size] != '\0') ++size;
    return size;
}

static EFI_STATUS open_root(EFI_HANDLE image_handle, EFI_BOOT_SERVICES *bs,
                            EFI_FILE_PROTOCOL **root) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_STATUS status = bs->handle_protocol(image_handle, &loaded_image_guid,
                                             (void **)&loaded);
    if (!EFI_ERROR(status) && loaded != 0 && loaded->device_handle != 0)
        status = bs->handle_protocol(loaded->device_handle, &simple_fs_guid,
                                     (void **)&fs);
    if (EFI_ERROR(status) || fs == 0) {
        debug_write("OPENCONSOLE_PHYSICAL_NET_FS_DEVICE_FALLBACK\n");
        status = bs->locate_protocol(&simple_fs_guid, 0, (void **)&fs);
    }
    if (EFI_ERROR(status) || fs == 0) return EFI_NOT_FOUND;
    return fs->open_volume(fs, root);
}

static EFI_STATUS open_evidence(EFI_FILE_PROTOCOL *root) {
    static const CHAR16 path[] = L"\\OPENCONSOLE-NETWORK-EVIDENCE.LOG";
    static uint8_t existing[EVIDENCE_CAPACITY];
    UINTN size = sizeof(existing), used = 0u;
    EFI_STATUS status = root->open(root, &evidence_file, path,
                                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0u);
    if (EFI_ERROR(status)) return status;
    status = evidence_file->read(evidence_file, &size, existing);
    if (EFI_ERROR(status)) return status;
    while (used < size && existing[used] != 0u) ++used;
    if (used > EVIDENCE_CAPACITY - 2048u) used = 0u;
    return evidence_file->set_position(evidence_file, used);
}

static EFI_STATUS read_config(EFI_FILE_PROTOCOL *root, char *bytes, UINTN capacity,
                              physical_config *config) {
    static const CHAR16 path[] = L"\\OPENCONSOLE-NETWORK.CFG";
    EFI_FILE_PROTOCOL *file = 0;
    UINTN size = capacity - 1u, cursor = 0u;
    EFI_STATUS status = root->open(root, &file, path, EFI_FILE_MODE_READ, 0u);
    if (EFI_ERROR(status)) return status;
    status = file->read(file, &size, bytes);
    (void)file->close(file);
    if (EFI_ERROR(status) || size == 0u || size >= capacity) return EFI_BUFFER_TOO_SMALL;
    bytes[size] = '\0';
    while (cursor < size) {
        char *key = bytes + cursor, *value = 0;
        while (cursor < size && bytes[cursor] != '\n' && bytes[cursor] != '\r') {
            if (bytes[cursor] == '=' && value == 0) {
                bytes[cursor] = '\0'; value = bytes + cursor + 1u;
            }
            ++cursor;
        }
        while (cursor < size && (bytes[cursor] == '\n' || bytes[cursor] == '\r'))
            bytes[cursor++] = '\0';
        if (value == 0 || key[0] == '#') continue;
#define CONFIG_KEY(name, member) \
        if (bytes_equal(key, name, sizeof(name)) && key[sizeof(name)-1u] == '\0') \
            config->member = value; else
        CONFIG_KEY("dns_name", dns_name)
        CONFIG_KEY("expected_dns_ipv4", expected_dns_ipv4)
        CONFIG_KEY("endpoint_ipv4", endpoint_ipv4)
        CONFIG_KEY("udp_port", udp_port)
        CONFIG_KEY("udp_request", udp_request)
        CONFIG_KEY("udp_response", udp_response)
        CONFIG_KEY("https_url", https_url)
        CONFIG_KEY("https_response", https_response) { }
#undef CONFIG_KEY
    }
    return config->dns_name && config->endpoint_ipv4 && config->udp_port &&
           config->udp_request && config->udp_response && config->https_url &&
           config->https_response ? EFI_SUCCESS : EFI_NOT_FOUND;
}

static int parse_ipv4(const char *text, uint32_t *address) {
    uint32_t octets[4] = {0u,0u,0u,0u}; UINTN part = 0u;
    if (text == 0 || *text == '\0') return 0;
    while (*text != '\0') {
        if (*text >= '0' && *text <= '9') {
            octets[part] = octets[part] * 10u + (uint32_t)(*text - '0');
            if (octets[part] > 255u) return 0;
        } else if (*text == '.' && part < 3u) ++part;
        else return 0;
        ++text;
    }
    if (part != 3u) return 0;
    *address = octets[0] | (octets[1] << 8u) | (octets[2] << 16u) | (octets[3] << 24u);
    return 1;
}

static int parse_port(const char *text, uint16_t *port) {
    uint32_t value = 0u;
    if (text == 0 || *text == '\0') return 0;
    while (*text != '\0') {
        if (*text < '0' || *text > '9') return 0;
        value = value * 10u + (uint32_t)(*text++ - '0');
        if (value > 65535u) return 0;
    }
    *port = (uint16_t)value; return value != 0u;
}

static EFI_STATUS await(OC_NET_PROTOCOL *net, OC_NET_SESSION *session,
                        OC_NET_OPERATION *operation, EFI_BOOT_SERVICES *bs,
                        uint64_t *now, OC_NET_OPERATION_STATUS *result) {
    for (;;) {
        EFI_STATUS status;
        result->structure_size = sizeof(*result);
        status = net->query_operation(operation, result);
        if (EFI_ERROR(status) || result->result != OC_NET_RESULT_PENDING) return status;
        *now += 1000u; status = net->pump(session, *now);
        if (EFI_ERROR(status)) return status;
        (void)bs->stall(1000u);
    }
}

static EFI_STATUS finish(OC_NET_PROTOCOL *net, OC_NET_OPERATION **operation,
                         OC_NET_OPERATION_STATUS *result) {
    EFI_STATUS status = result->result == OC_NET_RESULT_OK ? EFI_SUCCESS : EFI_DEVICE_ERROR;
    if (EFI_ERROR(net->release_operation(*operation))) status = EFI_DEVICE_ERROR;
    *operation = 0; return status;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    static char config_bytes[CONFIG_CAPACITY];
    static uint8_t response[4096];
    physical_config config = {0};
    EFI_FILE_PROTOCOL *root = 0;
    OC_NET_PROTOCOL *net = 0; OC_NET_SESSION *session = 0; OC_NET_OPERATION *operation = 0;
    OC_NET_SESSION_DESC session_desc = {sizeof(session_desc),OC_NET_ABI_VERSION,65536u,8u,0u};
    OC_NET_CAPABILITIES caps = {sizeof(caps),0u,0u,0u,0u,0u,0u,{0u}};
    OC_NET_OPERATION_STATUS result; OC_NET_STATE state = {sizeof(state),0u,0u,0u,0u,0u,0u};
    OC_NET_DNS_RESULT dns = {sizeof(dns),0u,{0u},0u};
    OC_NET_DEADLINE deadline; OC_NET_TRANSFER_DESC transfer; OC_NET_HTTPS_DESC https;
    uint32_t endpoint, expected_dns = 0u; uint16_t port; uint64_t now = 0u, started;
    UINTN request_size, expected_size, attempt; EFI_STATUS status;

    debug_write("OPENCONSOLE_PHYSICAL_NET_CHILD_ENTERED\n");
    status = open_root(image_handle, system_table->boot_services, &root);
    if (EFI_ERROR(status)) {
        debug_write("OPENCONSOLE_PHYSICAL_NET_FS_OPEN_FAILED\n");
        return status;
    }
    status = open_evidence(root);
    if (EFI_ERROR(status)) {
        debug_write("OPENCONSOLE_PHYSICAL_NET_EVIDENCE_OPEN_FAILED\n");
        (void)root->close(root);
        return status;
    }
    evidence("OPENCONSOLE_PHYSICAL_NET_RUN_BEGIN\n");
    status = read_config(root, config_bytes, sizeof(config_bytes), &config);
    if (EFI_ERROR(status)) goto fail;
    evidence("OPENCONSOLE_PHYSICAL_NET_FIXTURE_");
    evidence(config.udp_request);
    evidence("\n");
    if (!parse_ipv4(config.endpoint_ipv4, &endpoint) ||
        !parse_port(config.udp_port, &port) ||
        (config.expected_dns_ipv4 && config.expected_dns_ipv4[0] != '\0' &&
         !parse_ipv4(config.expected_dns_ipv4, &expected_dns))) {
        status = EFI_INVALID_PARAMETER;
        goto fail;
    }
    status = system_table->boot_services->locate_protocol(
        &OC_NET_PROTOCOL_GUID, 0, (void **)&net);
    if (EFI_ERROR(status) || net == 0 || net->abi_version != OC_NET_ABI_VERSION ||
        EFI_ERROR(net->query_capabilities(&caps)) ||
        (caps.capabilities & (OC_NET_CAP_DHCP4|OC_NET_CAP_DNS4|OC_NET_CAP_UDP4|
         OC_NET_CAP_HTTPS|OC_NET_CAP_TLS_IDENTITY_VALIDATION)) !=
        (OC_NET_CAP_DHCP4|OC_NET_CAP_DNS4|OC_NET_CAP_UDP4|
         OC_NET_CAP_HTTPS|OC_NET_CAP_TLS_IDENTITY_VALIDATION) ||
        EFI_ERROR(net->create_session(&session_desc, &session))) goto fail;

    started = now; deadline = (OC_NET_DEADLINE){sizeof(deadline),0u,now+15000000u};
    if (EFI_ERROR(net->configure_dhcp4(session,&deadline,&operation)) ||
        EFI_ERROR(await(net,session,operation,system_table->boot_services,&now,&result)) ||
        EFI_ERROR(finish(net,&operation,&result)) ||
        EFI_ERROR(net->query_state(session,&state)) || !state.link_up || !state.configured) goto fail;
    evidence("OPENCONSOLE_PHYSICAL_NET_DHCP_OK_USEC_"); evidence_hex32((uint32_t)(now-started));
    evidence("OPENCONSOLE_PHYSICAL_NET_ADDRESS_"); evidence_hex32(state.address_be);

    started = now; deadline.deadline_usec = now + 10000000u;
    if (EFI_ERROR(net->resolve_dns4(session,config.dns_name,
            text_size(config.dns_name,OC_NET_MAX_HOST_BYTES+1u),&dns,&deadline,&operation)) ||
        EFI_ERROR(await(net,session,operation,system_table->boot_services,&now,&result)) ||
        EFI_ERROR(finish(net,&operation,&result)) || dns.address_count == 0u ||
        (expected_dns != 0u && dns.addresses_be[0] != expected_dns)) goto fail;
    evidence("OPENCONSOLE_PHYSICAL_NET_DNS_OK_USEC_"); evidence_hex32((uint32_t)(now-started));
    evidence("OPENCONSOLE_PHYSICAL_NET_DNS_ADDRESS_"); evidence_hex32(dns.addresses_be[0]);

    request_size = text_size(config.udp_request,256u);
    expected_size = text_size(config.udp_response,256u); started = now;
    transfer = (OC_NET_TRANSFER_DESC){sizeof(transfer),endpoint,port,0u,
        config.udp_request,request_size,response,sizeof(response),
        {sizeof(OC_NET_DEADLINE),0u,now+5000000u}};
    if (request_size == 256u || expected_size == 256u ||
        EFI_ERROR(net->start_udp4(session,&transfer,&operation)) ||
        EFI_ERROR(await(net,session,operation,system_table->boot_services,&now,&result)) ||
        result.bytes_transferred != expected_size ||
        !bytes_equal(response,config.udp_response,expected_size) ||
        EFI_ERROR(finish(net,&operation,&result))) goto fail;
    evidence("OPENCONSOLE_PHYSICAL_NET_UDP_OK_USEC_"); evidence_hex32((uint32_t)(now-started));

    expected_size = text_size(config.https_response,sizeof(response)); started = now;
    https = (OC_NET_HTTPS_DESC){sizeof(https),0u,config.https_url,
        (uint32_t)text_size(config.https_url,OC_NET_MAX_URL_BYTES+1u),0,0u,
        response,sizeof(response),{sizeof(OC_NET_DEADLINE),0u,now+30000000u}};
    if (https.url_size > OC_NET_MAX_URL_BYTES || expected_size == sizeof(response)) goto fail;
    for (attempt=0u;attempt<100u;++attempt) {
        status=net->start_https(session,&https,&operation);
        if (status!=EFI_NO_MAPPING) break;
        now+=100000u; (void)net->pump(session,now); (void)system_table->boot_services->stall(100000u);
    }
    if (EFI_ERROR(status) ||
        EFI_ERROR(await(net,session,operation,system_table->boot_services,&now,&result)) ||
        result.bytes_transferred != expected_size ||
        !bytes_equal(response,config.https_response,expected_size) ||
        EFI_ERROR(finish(net,&operation,&result))) goto fail;
    evidence("OPENCONSOLE_PHYSICAL_NET_HTTPS_OK_USEC_"); evidence_hex32((uint32_t)(now-started));
    (void)net->destroy_session(session); session=0;
    evidence("OPENCONSOLE_PHYSICAL_NET_RUN_COMPLETE\n");
    if (evidence_file) { (void)evidence_file->flush(evidence_file); (void)evidence_file->close(evidence_file); }
    (void)root->close(root); return EFI_SUCCESS;
fail:
    evidence("OPENCONSOLE_PHYSICAL_NET_RUN_FAILED_STATUS_"); evidence_hex32((uint32_t)status);
    if (operation && net) (void)net->release_operation(operation);
    if (session && net) (void)net->destroy_session(session);
    if (evidence_file) { (void)evidence_file->flush(evidence_file); (void)evidence_file->close(evidence_file); }
    (void)root->close(root); return EFI_DEVICE_ERROR;
}
