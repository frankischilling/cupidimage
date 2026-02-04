#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

int cupidimage_kitty_delete_all(FILE *out, char *err, size_t errcap);
int cupidimage_kitty_delete_image(FILE *out, uint32_t image_id, char *err, size_t errcap);

void test_delete_all(void) {
    FILE *f = fmemopen(NULL, 256, "w");
    char err[128] = {0};

    int ret = cupidimage_kitty_delete_all(f, err, sizeof(err));
    assert(ret == 1);

    fclose(f);
}

void test_delete_image(void) {
    FILE *f = fmemopen(NULL, 256, "w");
    char err[128] = {0};

    int ret = cupidimage_kitty_delete_image(f, 42, err, sizeof(err));
    assert(ret == 1);

    fclose(f);
}

int main(void) {
    test_delete_all();
    test_delete_image();
    printf("All delete tests: PASS\n");
    return 0;
}
