#include <cstdlib>
#include <stdio.h>

struct foo {
    int x;
    char y[];
};

int main() {
    struct foo* sup = (struct foo*)malloc(sizeof(struct foo) + 4 * sizeof(char));
    sup->x = 69;
    sup->y[0] = 'a';
    sup->y[1] = 'b';
    sup->y[2] = 'c';
    sup->y[3] = '\0';
    
    printf("sup %d %s\n", sup->x, sup->y);
    free(sup);
    return 0;
}
