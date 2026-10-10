#include "node_api/NodeApi.h"

int main(void) {
    const NodeApi_Version version = NodeApi_GetVersion();
    return (version.major == 1U && version.minor == 0U) ? 0 : 1;
}
