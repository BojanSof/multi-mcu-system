#include <stddef.h>
void mbedtls_platform_zeroize(void *buffer, size_t size) {
    volatile unsigned char *data = buffer;
    while (size-- != 0U)
        *data++ = 0U;
}
