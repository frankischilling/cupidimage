#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int cupidimage_is_kitty_terminal(void);

void test_detection_no_vars(void) {
    unsetenv("TERM");
    unsetenv("KITTY_WINDOW_ID");
    assert(cupidimage_is_kitty_terminal() == 0);
}

void test_detection_kitty_term(void) {
    setenv("TERM", "xterm-kitty", 1);
    unsetenv("KITTY_WINDOW_ID");
    assert(cupidimage_is_kitty_terminal() == 1);
}

void test_detection_kitty_window_id(void) {
    setenv("TERM", "xterm-256color", 1);
    setenv("KITTY_WINDOW_ID", "1", 1);
    assert(cupidimage_is_kitty_terminal() == 1);
}

int main(void) {
    test_detection_no_vars();
    test_detection_kitty_term();
    test_detection_kitty_window_id();
    printf("All detection tests: PASS\n");
    return 0;
}
