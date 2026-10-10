#include "node_api/NodeApi.h"

NodeApi_Version NodeApi_GetVersion(void) {
    const NodeApi_Version version = {NODE_API_VERSION_MAJOR, NODE_API_VERSION_MINOR};
    return version;
}
