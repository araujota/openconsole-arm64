#include "openconsole/audio_protocol.h"
#include "openconsole/net_protocol.h"

#include <stddef.h>
#include <stdio.h>

_Static_assert(sizeof(OC_NET_CAPABILITIES) == 48u, "network capabilities ABI drift");
_Static_assert(sizeof(OC_NET_SESSION_DESC) == 24u, "network session ABI drift");
_Static_assert(sizeof(OC_NET_STATE) == 32u, "network state ABI drift");
_Static_assert(sizeof(OC_NET_OPERATION_STATUS) == 32u, "network operation ABI drift");
_Static_assert(offsetof(OC_NET_PROTOCOL, create_session) == 16u, "network protocol table ABI drift");
_Static_assert(sizeof(OC_NET_PROTOCOL) == 112u, "network protocol size drift");

_Static_assert(sizeof(OC_AUDIO_CAPABILITIES) == 56u, "audio capabilities ABI drift");
_Static_assert(sizeof(OC_AUDIO_OUTPUT_INFO) == 80u, "audio output ABI drift");
_Static_assert(sizeof(OC_AUDIO_VOICE_DESC) == 32u, "audio voice description ABI drift");
_Static_assert(sizeof(OC_AUDIO_BUFFER) == 32u, "audio buffer ABI drift");
_Static_assert(sizeof(OC_AUDIO_VOICE_STATUS) == 56u, "audio voice status ABI drift");
_Static_assert(offsetof(OC_AUDIO_PROTOCOL, create_voice) == 32u, "audio protocol table ABI drift");
_Static_assert(sizeof(OC_AUDIO_PROTOCOL) == 112u, "audio protocol size drift");

int main(void) {
    if (!oc_net_abi_compatible(OC_NET_ABI_VERSION)) return 1;
    if (!oc_net_abi_compatible(0u)) return 2;
    if (oc_net_abi_compatible((1u << 16) | OC_NET_ABI_MINOR)) return 3;
    if (oc_net_abi_compatible(OC_NET_ABI_VERSION + 1u)) return 4;
    if (!oc_audio_abi_compatible(OC_AUDIO_ABI_VERSION)) return 5;
    if (!oc_audio_abi_compatible(0u)) return 6;
    if (oc_audio_abi_compatible((1u << 16) | OC_AUDIO_ABI_MINOR)) return 7;
    if (oc_audio_abi_compatible(OC_AUDIO_ABI_VERSION + 1u)) return 8;
    puts("PASS: network/audio ABI layout and incompatible-version rejection");
    return 0;
}
