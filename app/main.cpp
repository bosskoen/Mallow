#include "core/io/terminal.h"
#include "fs/path.h"
int32 mallowMain()
{
	println("{}", fs::PathBuffer::getWorkingDir());
	println("{}" , fs::PathBuffer::absolute("this/is/../a/path"));
	fs::PathBuffer b{fs::Path{"some/path"}};
	b.absolute();
	println("{}", b);
	return 0;
}