#include "core/io/io.h"
#include <core/macro.h>
#include <core/error.h>

#include <fs/file.h>

#include <iter/iterator.h>
#include <stl/string.h>

#include <iter/lines.h>
int32 mallowMain()
{
	fs::File::open(core::CStr("test.txt"), io::OpenMode::Append);
	if_ok(file, fs::File::open(core::CStr("test.txt"), io::OpenMode::ReadWrite), {
		file.write(core::asBytes(core::CStr("Hello, world!\n")));
		file.seek(io::SeekFrom::Start, 0);
		core::Vector<uint8> contents = file.readAll().takeValue();
		println("File contents: {}", contents);
		})

	core::Vector<core::String> v;
	for (int i = 0; i < 10; ++i)
	{
		v.push(core::CStr("x"));
	}

	// core::move(v).drain();
	// auto range = itorator::range_for(itorator::borrow_iter(v)).begin().operator*().clone();

	// core::Vector<core::String> r = itorator::borrow_iter(v).collect<core::Vector<core::String>>();
	for (const core::String &s : itorator::borrow_iter(v).range())
	{
		println("v: {}", s);
	}

	return 0;
}