#include "core/io/io.h"
#include <core/macro.h>
#include <core/error.h>

#include <fs/file.h>

#include <iter/iterator.h>
#include <stl/string.h>

int32 mallowMain()
{
	if_ok(file, fs::File::open(core::CStr("test.txt"), io::OpenMode::ReadWrite), {
		file.write(core::asBytes(core::CStr("Hello, world!\n")));
		core::Vector<uint8> contents = file.readAll().takeValue();
		println("File contents: {}", contents);
	})

	core::Vector<core::CStr> v;
	for (int i = 0; i < 10; ++i){
		v.push(core::CStr("x"));
	}

	auto range = itorator::range_for(itorator::borrow_iter(v)).begin().operator*();
	for (const core::CStr &s : itorator::range_for(itorator::borrow_iter(v)))
	{
		println("v: {}", s);
	}

	return 0;
}