#include <core/io.h>
#include <core/terminal.h>
int mallowMain(){
    core::io::write(core::terminal::stdoutHandle(), "hello world");

    return 0;
}