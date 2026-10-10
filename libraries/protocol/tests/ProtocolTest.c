#include "protocol/Protocol.h"

int main(void) {
    const Protocol_Version version = Protocol_GetVersion();
    return (version.major == PROTOCOL_API_VERSION_MAJOR &&
            version.minor == PROTOCOL_API_VERSION_MINOR)
               ? 0
               : 1;
}
