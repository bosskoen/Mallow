#include "core/io/terminal.h"
extern int mallowMain();

int main(int c, const char *v[]){
    core::terminal::args.count = c;
    core::terminal::args.values = v;

    return mallowMain();
}