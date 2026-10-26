#include "openconsole/manifest.h"
#include "openconsole/target.h"

enum field_bits {
    FIELD_ID = 1u << 0,
    FIELD_TITLE = 1u << 1,
    FIELD_ENTRY = 1u << 2,
    FIELD_ARCH = 1u << 3,
    FIELD_PROTOCOLS = 1u << 4,
    FIELD_GRAPHICS = 1u << 5,
    FIELD_ACCEL = 1u << 6,
    FIELD_ABI = 1u << 7,
    FIELD_DEVELOPER = 1u << 8,
    FIELD_DESCRIPTION = 1u << 9,
    FIELD_VERSION = 1u << 10,
    FIELD_WEBSITE = 1u << 11,
    FIELD_FORMAT_VERSION = 1u << 12,
    FIELD_PACKAGE_FORMAT = 1u << 13,
    FIELD_REQUIRED_CAPABILITIES = 1u << 14,
    FIELD_OPTIONAL_CAPABILITIES = 1u << 15,
    FIELD_JACKET = 1u << 16,
    FIELD_CONTROLS = 1u << 17
};

#define V1_REQUIRED_FIELDS ((1u << 8) - 1u)
#define V2_REQUIRED_FIELDS (FIELD_ID | FIELD_TITLE | FIELD_ENTRY | FIELD_ARCH | \
    FIELD_GRAPHICS | FIELD_ACCEL | FIELD_ABI | FIELD_VERSION | \
    FIELD_FORMAT_VERSION | FIELD_PACKAGE_FORMAT | FIELD_REQUIRED_CAPABILITIES | \
    FIELD_OPTIONAL_CAPABILITIES)

typedef struct {
    const char *cursor;
    const char *end;
    uint32_t line;
} parser;

static void zero_bytes(void *destination, size_t size) {
    unsigned char *bytes = destination;
    while (size-- != 0u) {
        *bytes++ = 0u;
    }
}

static size_t string_length(const char *text) {
    size_t size = 0u;
    while (text[size] != '\0') {
        ++size;
    }
    return size;
}

static int string_equal(const char *left, const char *right) {
    size_t i = 0u;
    while (left[i] != '\0' && left[i] == right[i]) {
        ++i;
    }
    return left[i] == right[i];
}

static int ascii_alpha(char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

static int ascii_digit(char value) {
    return value >= '0' && value <= '9';
}

static void skip_horizontal(parser *p) {
    while (p->cursor != p->end && (*p->cursor == ' ' || *p->cursor == '\t')) {
        ++p->cursor;
    }
}

static int at_line_end(const parser *p) {
    return p->cursor == p->end || *p->cursor == '\r' || *p->cursor == '\n' ||
           *p->cursor == '#';
}

static void consume_line_end(parser *p) {
    if (p->cursor != p->end && *p->cursor == '#') {
        while (p->cursor != p->end && *p->cursor != '\r' && *p->cursor != '\n') {
            ++p->cursor;
        }
    }
    if (p->cursor != p->end && *p->cursor == '\r') {
        ++p->cursor;
        if (p->cursor != p->end && *p->cursor == '\n') {
            ++p->cursor;
        }
        ++p->line;
    } else if (p->cursor != p->end && *p->cursor == '\n') {
        ++p->cursor;
        ++p->line;
    }
}

static int parse_key(parser *p, char *out, size_t capacity) {
    size_t size = 0u;
    while (p->cursor != p->end) {
        char value = *p->cursor;
        if (!(ascii_alpha(value) || ascii_digit(value) || value == '_')) {
            break;
        }
        if (size + 1u >= capacity) {
            return 0;
        }
        out[size++] = value;
        ++p->cursor;
    }
    out[size] = '\0';
    return size != 0u;
}

static int parse_string(parser *p, char *out, size_t capacity) {
    size_t size = 0u;
    if (p->cursor == p->end || *p->cursor != '"') {
        return 0;
    }
    ++p->cursor;
    while (p->cursor != p->end && *p->cursor != '"') {
        unsigned char value = (unsigned char)*p->cursor++;
        if (value == '\\') {
            if (p->cursor == p->end) {
                return 0;
            }
            value = (unsigned char)*p->cursor++;
            if (value == 'n') value = '\n';
            else if (value == 'r') value = '\r';
            else if (value == 't') value = '\t';
            else if (value != '"' && value != '\\') return 0;
        }
        if (value < 0x20u || size + 1u >= capacity) {
            return 0;
        }
        out[size++] = (char)value;
    }
    if (p->cursor == p->end || *p->cursor != '"') {
        return 0;
    }
    ++p->cursor;
    out[size] = '\0';
    return 1;
}

static int parse_boolean(parser *p, uint8_t *out) {
    const char *start = p->cursor;
    const char true_value[] = "true";
    const char false_value[] = "false";
    size_t i;
    for (i = 0u; i < sizeof(true_value) - 1u && p->cursor != p->end; ++i) {
        if (*p->cursor++ != true_value[i]) break;
    }
    if (i == sizeof(true_value) - 1u) {
        *out = 1u;
        return 1;
    }
    p->cursor = start;
    for (i = 0u; i < sizeof(false_value) - 1u && p->cursor != p->end; ++i) {
        if (*p->cursor++ != false_value[i]) break;
    }
    if (i == sizeof(false_value) - 1u) {
        *out = 0u;
        return 1;
    }
    p->cursor = start;
    return 0;
}

static int parse_u32(parser *p, uint32_t *out) {
    uint32_t value = 0u;
    int found = 0;
    while (p->cursor != p->end && ascii_digit(*p->cursor)) {
        uint32_t digit = (uint32_t)(*p->cursor++ - '0');
        if (value > (UINT32_MAX - digit) / 10u) {
            return 0;
        }
        value = value * 10u + digit;
        found = 1;
    }
    *out = value;
    return found;
}

static int parse_string_array(parser *p, char values[][65], uint32_t *count,
                              uint32_t maximum, oc_manifest_error *error) {
    if (p->cursor == p->end || *p->cursor != '[') return 0;
    ++p->cursor;
    skip_horizontal(p);
    if (p->cursor != p->end && *p->cursor == ']') {
        ++p->cursor;
        return 1;
    }
    for (;;) {
        if (*count == maximum) {
            *error = OC_MANIFEST_TOO_MANY_PROTOCOLS;
            return 0;
        }
        if (!parse_string(p, values[*count], sizeof(values[0]))) {
            return 0;
        }
        if (values[*count][0] == '\0') {
            return 0;
        }
        ++*count;
        skip_horizontal(p);
        if (p->cursor != p->end && *p->cursor == ']') {
            ++p->cursor;
            return 1;
        }
        if (p->cursor == p->end || *p->cursor != ',') return 0;
        ++p->cursor;
        skip_horizontal(p);
    }
}

static uint32_t field_for_key(const char *key) {
    if (string_equal(key, "id")) return FIELD_ID;
    if (string_equal(key, "title")) return FIELD_TITLE;
    if (string_equal(key, "entry")) return FIELD_ENTRY;
    if (string_equal(key, "architecture")) return FIELD_ARCH;
    if (string_equal(key, "required_protocols")) return FIELD_PROTOCOLS;
    if (string_equal(key, "graphics_api")) return FIELD_GRAPHICS;
    if (string_equal(key, "accel")) return FIELD_ACCEL;
    if (string_equal(key, "min_graphics_abi")) return FIELD_ABI;
    if (string_equal(key, "developer")) return FIELD_DEVELOPER;
    if (string_equal(key, "description")) return FIELD_DESCRIPTION;
    if (string_equal(key, "version")) return FIELD_VERSION;
    if (string_equal(key, "website")) return FIELD_WEBSITE;
    if (string_equal(key, "format_version")) return FIELD_FORMAT_VERSION;
    if (string_equal(key, "package_format")) return FIELD_PACKAGE_FORMAT;
    if (string_equal(key, "required_capabilities")) return FIELD_REQUIRED_CAPABILITIES;
    if (string_equal(key, "optional_capabilities")) return FIELD_OPTIONAL_CAPABILITIES;
    if (string_equal(key, "jacket")) return FIELD_JACKET;
    if (string_equal(key, "controls")) return FIELD_CONTROLS;
    return 0u;
}

static int valid_id(const char *id) {
    size_t i;
    size_t size = string_length(id);
    if (size == 0u || size > 64u || !((id[0] >= 'a' && id[0] <= 'z') || ascii_digit(id[0]))) {
        return 0;
    }
    for (i = 1u; i < size; ++i) {
        char value = id[i];
        if (!((value >= 'a' && value <= 'z') || ascii_digit(value) ||
              value == '-' || value == '_')) {
            return 0;
        }
    }
    return 1;
}

static int valid_version(const char *value) {
    size_t i, size = string_length(value);
    if (size == 0u || size > 32u ||
        !(ascii_alpha(value[0]) || ascii_digit(value[0]))) return 0;
    for (i = 1u; i < size; ++i) {
        char byte = value[i];
        if (!(ascii_alpha(byte) || ascii_digit(byte) || byte == '.' || byte == '+' ||
              byte == '_' || byte == '-')) return 0;
    }
    return 1;
}

static int valid_capability(const char *value) {
    size_t i = 0u, name_size = 0u;
    unsigned major_digits = 0u, minor_digits = 0u;
    while (value[i] != '\0' && value[i] != '@') {
        char byte = value[i++];
        if (!((byte >= 'a' && byte <= 'z') || ascii_digit(byte) || byte == '.' ||
              byte == '-')) return 0;
        ++name_size;
    }
    if (name_size == 0u || value[i++] != '@') return 0;
    while (ascii_digit(value[i])) { ++major_digits; ++i; }
    if (major_digits == 0u || value[i++] != '.') return 0;
    while (ascii_digit(value[i])) { ++minor_digits; ++i; }
    return minor_digits != 0u && value[i] == '\0';
}

static int capabilities_valid(char values[][65], uint32_t count) {
    uint32_t i, j;
    for (i = 0u; i < count; ++i) {
        if (!valid_capability(values[i])) return 0;
        for (j = 0u; j < i; ++j)
            if (string_equal(values[i], values[j])) return 0;
    }
    return 1;
}

int oc_manifest_relative_path_is_safe(const char *path) {
    size_t i = 0u;
    size_t component_size = 0u;
    if (path == 0 || path[0] == '\0' || path[0] == '/' || path[0] == '\\') return 0;
    if (ascii_alpha(path[0]) && path[1] == ':') return 0;
    for (;;) {
        unsigned char value = (unsigned char)path[i];
        if (value == '\\' || (value != 0u && value < 0x20u) || value == 0x7fu) return 0;
        if (value == '/' || value == '\0') {
            const char *component = path + i - component_size;
            if (component_size == 0u ||
                (component_size == 1u && component[0] == '.') ||
                (component_size == 2u && component[0] == '.' && component[1] == '.')) {
                return 0;
            }
            if (value == '\0') return 1;
            component_size = 0u;
        } else {
            ++component_size;
        }
        ++i;
    }
}

static oc_manifest_result result(oc_manifest_error code, uint32_t line) {
    oc_manifest_result value = {code, line};
    return value;
}

oc_manifest_result oc_manifest_parse(
    const char *source, size_t source_size, oc_game_manifest *out) {
    parser p;
    uint32_t fields = 0u;
    int accel_was_policy = 0;
    if (source == 0 || out == 0) return result(OC_MANIFEST_INVALID_VALUE, 0u);
    if (source_size > OC_MANIFEST_MAX_BYTES) return result(OC_MANIFEST_TOO_LARGE, 0u);
    zero_bytes(out, sizeof(*out));
    p.cursor = source;
    p.end = source + source_size;
    p.line = 1u;
    while (p.cursor != p.end) {
        char key[32];
        uint32_t field;
        oc_manifest_error parse_error = OC_MANIFEST_SYNTAX;
        skip_horizontal(&p);
        if (at_line_end(&p)) {
            consume_line_end(&p);
            continue;
        }
        if (!parse_key(&p, key, sizeof(key))) return result(OC_MANIFEST_SYNTAX, p.line);
        field = field_for_key(key);
        if (field == 0u) return result(OC_MANIFEST_UNKNOWN_KEY, p.line);
        if ((fields & field) != 0u) return result(OC_MANIFEST_DUPLICATE_KEY, p.line);
        skip_horizontal(&p);
        if (p.cursor == p.end || *p.cursor++ != '=') return result(OC_MANIFEST_SYNTAX, p.line);
        skip_horizontal(&p);
        if (field == FIELD_ID) {
            if (!parse_string(&p, out->id, sizeof(out->id))) return result(parse_error, p.line);
        } else if (field == FIELD_TITLE) {
            if (!parse_string(&p, out->title, sizeof(out->title))) return result(parse_error, p.line);
        } else if (field == FIELD_ENTRY) {
            if (!parse_string(&p, out->entry, sizeof(out->entry))) return result(parse_error, p.line);
        } else if (field == FIELD_ARCH) {
            if (!parse_string(&p, out->architecture, sizeof(out->architecture))) return result(parse_error, p.line);
        } else if (field == FIELD_PROTOCOLS) {
            if (!parse_string_array(&p, out->required_protocols,
                                    &out->required_protocol_count,
                                    OC_MANIFEST_MAX_PROTOCOLS, &parse_error))
                return result(parse_error, p.line);
        } else if (field == FIELD_GRAPHICS) {
            char graphics[16];
            if (!parse_string(&p, graphics, sizeof(graphics))) return result(parse_error, p.line);
            if (string_equal(graphics, "gop")) out->graphics_api = OC_GRAPHICS_GOP;
            else if (string_equal(graphics, "gles2")) out->graphics_api = OC_GRAPHICS_GLES2;
            else return result(OC_MANIFEST_INVALID_VALUE, p.line);
        } else if (field == FIELD_ACCEL) {
            if (p.cursor != p.end && *p.cursor == '"') {
                char policy[16];
                if (!parse_string(&p, policy, sizeof(policy))) return result(parse_error, p.line);
                accel_was_policy = 1;
                if (string_equal(policy, "forbidden")) out->accel_policy = OC_ACCEL_FORBIDDEN;
                else if (string_equal(policy, "optional")) out->accel_policy = OC_ACCEL_OPTIONAL;
                else if (string_equal(policy, "required")) out->accel_policy = OC_ACCEL_REQUIRED;
                else return result(OC_MANIFEST_INVALID_VALUE, p.line);
                out->accel = out->accel_policy != OC_ACCEL_FORBIDDEN;
            } else {
                if (!parse_boolean(&p, &out->accel)) return result(parse_error, p.line);
                out->accel_policy = out->accel ? OC_ACCEL_OPTIONAL : OC_ACCEL_FORBIDDEN;
            }
        } else if (field == FIELD_ABI) {
            if (!parse_u32(&p, &out->min_graphics_abi)) return result(parse_error, p.line);
        } else if (field == FIELD_DEVELOPER) {
            if (!parse_string(&p, out->developer, sizeof(out->developer))) return result(parse_error, p.line);
        } else if (field == FIELD_DESCRIPTION) {
            if (!parse_string(&p, out->description, sizeof(out->description))) return result(parse_error, p.line);
        } else if (field == FIELD_VERSION) {
            if (!parse_string(&p, out->version, sizeof(out->version))) return result(parse_error, p.line);
        } else if (field == FIELD_WEBSITE) {
            if (!parse_string(&p, out->website, sizeof(out->website))) return result(parse_error, p.line);
        } else if (field == FIELD_FORMAT_VERSION) {
            if (!parse_string(&p, out->format_version, sizeof(out->format_version))) return result(parse_error, p.line);
        } else if (field == FIELD_PACKAGE_FORMAT) {
            if (!parse_string(&p, out->package_format, sizeof(out->package_format))) return result(parse_error, p.line);
        } else if (field == FIELD_REQUIRED_CAPABILITIES) {
            if (!parse_string_array(&p, out->required_capabilities,
                                    &out->required_capability_count,
                                    OC_MANIFEST_MAX_CAPABILITIES, &parse_error))
                return result(parse_error, p.line);
        } else if (field == FIELD_OPTIONAL_CAPABILITIES) {
            if (!parse_string_array(&p, out->optional_capabilities,
                                    &out->optional_capability_count,
                                    OC_MANIFEST_MAX_CAPABILITIES, &parse_error))
                return result(parse_error, p.line);
        } else if (field == FIELD_JACKET) {
            if (!parse_string(&p, out->jacket, sizeof(out->jacket))) return result(parse_error, p.line);
        } else if (field == FIELD_CONTROLS) {
            if (!parse_string(&p, out->controls, sizeof(out->controls))) return result(parse_error, p.line);
        }
        skip_horizontal(&p);
        if (!at_line_end(&p)) return result(OC_MANIFEST_SYNTAX, p.line);
        fields |= field;
        consume_line_end(&p);
    }
    if ((fields & FIELD_FORMAT_VERSION) != 0u) {
        if ((fields & V2_REQUIRED_FIELDS) != V2_REQUIRED_FIELDS ||
            (fields & FIELD_PROTOCOLS) != 0u)
            return result(OC_MANIFEST_MISSING_KEY, p.line);
        if (!string_equal(out->format_version, "2.0") ||
            !string_equal(out->package_format, "ocg2"))
            return result(OC_MANIFEST_INCOMPATIBLE, 0u);
        if (!accel_was_policy || !valid_version(out->version) ||
            !capabilities_valid(out->required_capabilities, out->required_capability_count) ||
            !capabilities_valid(out->optional_capabilities, out->optional_capability_count))
            return result(OC_MANIFEST_INVALID_VALUE, 0u);
        if ((fields & FIELD_JACKET) != 0u && !oc_manifest_relative_path_is_safe(out->jacket))
            return result(OC_MANIFEST_INVALID_PATH, 0u);
        if ((fields & FIELD_CONTROLS) != 0u && !oc_manifest_relative_path_is_safe(out->controls))
            return result(OC_MANIFEST_INVALID_PATH, 0u);
    } else {
        if ((fields & V1_REQUIRED_FIELDS) != V1_REQUIRED_FIELDS || accel_was_policy ||
            (fields & (FIELD_PACKAGE_FORMAT | FIELD_REQUIRED_CAPABILITIES |
                       FIELD_OPTIONAL_CAPABILITIES | FIELD_JACKET | FIELD_CONTROLS)) != 0u)
            return result(OC_MANIFEST_MISSING_KEY, p.line);
        out->format_version[0] = '1'; out->format_version[1] = '.';
        out->format_version[2] = '0'; out->format_version[3] = '\0';
    }
    if (!valid_id(out->id)) return result(OC_MANIFEST_INVALID_ID, 0u);
    if (out->title[0] == '\0') return result(OC_MANIFEST_INVALID_VALUE, 0u);
    if (!oc_manifest_relative_path_is_safe(out->entry)) return result(OC_MANIFEST_INVALID_PATH, 0u);
    if (!string_equal(out->architecture, OC_TARGET_ARCH_NAME))
        return result(OC_MANIFEST_INCOMPATIBLE, 0u);
    if (out->graphics_api == OC_GRAPHICS_GOP && out->accel_policy != OC_ACCEL_FORBIDDEN) {
        return result(OC_MANIFEST_INCOMPATIBLE, 0u);
    }
    return result(OC_MANIFEST_OK, 0u);
}

const char *oc_manifest_error_string(oc_manifest_error error) {
    static const char *const messages[] = {
        "ok", "manifest too large", "syntax error", "unknown key",
        "duplicate key", "missing required key", "value too long",
        "invalid id", "unsafe relative path", "invalid value",
        "too many protocols", "incompatible manifest"
    };
    if ((unsigned)error >= sizeof(messages) / sizeof(messages[0])) return "unknown error";
    return messages[error];
}
