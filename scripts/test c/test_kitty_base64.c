#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

/* Forward declaration */
int kitty_base64_encode(const uint8_t *data, size_t size, char *out, size_t outcap);

void test_base64_empty(void) {
    char out[16] = {0};
    int ret = kitty_base64_encode(NULL, 0, out, sizeof(out));
    assert(ret == 0);  /* 0 bytes encoded */
    assert(out[0] == '\0');  /* empty string */
}

void test_base64_standard(void) {
    char out[64] = {0};

    /* "Man" -> "TWFu" */
    const uint8_t data1[] = {'M', 'a', 'n'};
    int ret = kitty_base64_encode(data1, 3, out, sizeof(out));
    assert(ret == 4);
    assert(strcmp(out, "TWFu") == 0);

    /* "Ma" -> "TWE=" */
    const uint8_t data2[] = {'M', 'a'};
    ret = kitty_base64_encode(data2, 2, out, sizeof(out));
    assert(ret == 4);
    assert(strcmp(out, "TWE=") == 0);

    /* "M" -> "TQ==" */
    const uint8_t data3[] = {'M'};
    ret = kitty_base64_encode(data3, 1, out, sizeof(out));
    assert(ret == 4);
    assert(strcmp(out, "TQ==") == 0);
}

int main(void) {
    test_base64_empty();
    test_base64_standard();
    printf("All base64 tests: PASS\n");
    return 0;
}
