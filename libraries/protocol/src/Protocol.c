#include "protocol/Protocol.h"

Protocol_Version Protocol_GetVersion(void) {
    const Protocol_Version version = {PROTOCOL_API_VERSION_MAJOR, PROTOCOL_API_VERSION_MINOR};
    return version;
}
