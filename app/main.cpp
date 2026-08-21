#include "core/io/io.h"
#include <core/macro.h>
#include <core/error.h>

#include <fs/file.h>

int32 mallowMain()
{
	if_ok(file, fs::File::open(core::CStr("test.txt"), io::OpenMode::ReadWrite), {
		file.write(core::asBytes(core::CStr("Hello, world!\n")));
		core::Vector<uint8> contents = file.readAll().takeValue();
		println("File contents: {}", contents);
	})

	return 0;
}