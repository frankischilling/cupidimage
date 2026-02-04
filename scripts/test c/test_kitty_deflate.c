#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stddef.h>

/* Forward declarations */
int cupidimage_deflate_compress(const uint8_t *data, size_t size,
                                uint8_t *out, size_t outcap,
                                size_t *outlen);

void test_deflate_empty(void) {
    uint8_t out[64] = {0};
    size_t outlen = 0;
    int ret = cupidimage_deflate_compress(NULL, 0, out, sizeof(out), &outlen);
    assert(ret == 1);  /* success */
    assert(outlen >= 2);  /* at least zlib header */
}

void test_deflate_simple(void) {
    const uint8_t data[] = "Hello";
    uint8_t out[128] = {0};
    size_t outlen = 0;

    int ret = cupidimage_deflate_compress(data, 5, out, sizeof(out), &outlen);
    assert(ret == 1);
    assert(outlen > 2);  /* more than just header */

    /* Verify zlib header */
    assert(out[0] == 0x78);
    assert((out[1] & 0x1F) == 0x1C || (out[1] & 0x1F) == 0x9C);
}

int main(void) {
    test_deflate_empty();
    test_deflate_simple();
    printf("All deflate tests: PASS\n");
    return 0;
}
