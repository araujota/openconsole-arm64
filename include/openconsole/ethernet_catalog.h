#ifndef OPENCONSOLE_ETHERNET_CATALOG_H
#define OPENCONSOLE_ETHERNET_CATALOG_H

#include <stddef.h>
#include <stdint.h>

#define OC_ETH_CATALOG_MAX_ENTRIES 32u
#define OC_ETH_CATALOG_PATH_BYTES 96u
#define OC_ETH_CATALOG_ID_BYTES 32u

#define OC_ETH_PROTOCOL_NII 1u
#define OC_ETH_PROTOCOL_UNDI 2u
#define OC_ETH_PROTOCOL_SNP 4u

typedef enum {
    OC_ETH_CATALOG_OK = 0, OC_ETH_CATALOG_TRUNCATED,
    OC_ETH_CATALOG_MAGIC, OC_ETH_CATALOG_VERSION,
    OC_ETH_CATALOG_HEADER, OC_ETH_CATALOG_ENTRY
} oc_eth_catalog_result;

typedef struct {
    const uint8_t *path;
    uint16_t path_size;
    const uint8_t *manifest_id;
    uint16_t manifest_id_size;
    uint8_t architecture;
    uint16_t protocols;
    uint16_t vendor, device, subsystem_vendor, subsystem_device;
    uint8_t revision_min, revision_max;
    uint32_t priority;
    uint64_t artifact_size;
    const uint8_t *sha256;
} oc_eth_catalog_entry;

typedef struct {
    const uint8_t *blob;
    size_t blob_size;
    uint32_t entry_count;
} oc_eth_catalog;

oc_eth_catalog_result oc_eth_catalog_parse(const uint8_t *, size_t,
                                            oc_eth_catalog *);
oc_eth_catalog_result oc_eth_catalog_entry_at(const oc_eth_catalog *, uint32_t,
                                               oc_eth_catalog_entry *);
const char *oc_eth_catalog_result_code(oc_eth_catalog_result);

#endif
