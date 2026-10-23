#pragma once

#include <iter/iterator.h>
#include <stl/string.h>
#include "iter/streams.h"

namespace itorator
{

	template <BufRead R>
	class Lines : IterBase<Lines<R>>
	{
		R reader;
		core::String carry{}; // ONLY holds a partial line across a boundary
		bool done = false;

	public:
		using Item = core::Result<core::CStr, io::IoError>;

		core::Optional<Item> next()
		{
			carry.clear();
			for (;;)
			{
				core::Result<core::Span<const uint8>, io::IoError> r = reader.fillBuf();
				if (r.isErr())
					return core::Optional<Item>{core::optional_in_place, core::Err{r.takeError()}};
				core::Span<const uint8> b = r.value();

				if (b.len == 0)
				{ // EOF
					if (carry.len() != 0)
						return emitCarry(stripCRcarry());
					return {};
				}

				// scan the reader's OWN buffer for '\n' — no copy
				isize i = 0;
				while (i < b.len && b.ptr[i] != '\n')
					++i;

				if (i < b.len)
				{ // found '\n' at i
					if (carry.len() == 0)
					{
						// zero-copy: line is b.ptr[0 .. i), strip a trailing '\r' by shortening the view
						isize n = stripCRspan(b.ptr, i); // i, or i-1 if b.ptr[i-1]=='\r'
						core::CStr line{(const char *)b.ptr, n};
						reader.consume(i + 1); // consume the FULL line + '\n' (incl. the '\r')
						return emitView(line);
					}
					else
					{
						// carry path: append this buffer's piece, THEN strip from the combined carry
						carry.append(core::CStr{(const char *)b.ptr, i});
						reader.consume(i + 1);
						return emitCarry(stripCRcarry()); // emit carry minus a trailing '\r'
					}
				}
				else
				{ // no '\n' in this buffer: all partial
					carry.append(core::CStr{(const char *)b.ptr, b.len});
					reader.consume(b.len);
				}
			}
		}

	private:
		static isize stripCRspan(const uint8 *ptr, isize len)
		{
			if (len > 0 && ptr[len - 1] == '\r')
				return len - 1;
			return len;
		}

		// How many bytes of `carry` to emit, dropping a trailing '\r'.
		isize stripCRcarry() const
		{
			isize n = carry.len();
			if (n > 0 && (uint8)carry.data()[n - 1] == '\r')
				return n - 1;
			return n;
		}

		core::Optional<Item> emitView(core::CStr line)
		{
			return core::Optional<Item>{core::optional_in_place, core::Ok{line}};
		}
		core::Optional<Item> emitCarry(isize len)
		{
			return core::Optional<Item>{core::optional_in_place, core::Ok{carry.slice(0, len)}};
		}
	};

} // namespace itorator
