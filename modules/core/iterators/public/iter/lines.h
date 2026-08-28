#pragma once

#include <iter/iterator.h>
#include <stl/string.h>

namespace itorator
{
	template <ItoratorType It>
		requires core::is_same_v<typename It::Item, core::Result<core::Span<uint8>, io::IoError>>
	struct Lines : IterBase < Lines<It>>
	{
		It up;
		core::String str{};
		core::CStr buff{ 0,0 };
		isize pos = 0;

		Lines(It&& u) : up(core::move(u)) {};
		using Item = core::Result<core::CStr, io::IoError>;

		core::Optional<Item> next() {
			str.clear();
			for (;;) {
				if (pos != buff.len) {
					isize base = pos;
					for (; pos < buff.len; ++pos) {
						if (buff[pos] == '\n') {
							str.append(core::CStr(buff.ptr + base, pos - base)); // exclude \n
							++pos;                                                // consume it
							// optional: strip trailing '\r' from str here for CRLF
							return core::Optional<Item>{ core::optional_in_place,
								core::Ok{ str.slice(0, str.len()) } };
						}
					}
					str.append(core::CStr(buff.ptr + base, pos - base));         // tail, no newline yet
				}
				else {
					core::Optional<core::Result<core::Span<uint8>, io::IoError>> r = up.next();
					if (r.isNone()) {
						if (str.len() != 0) {
							return core::Optional<Item>{ core::optional_in_place,
								core::Ok{ str.slice(0, str.len()) } };
						}
						return {};                                                // clean EOF
					}
					if (r->isErr()) {
						return core::Optional<Item>{ core::optional_in_place,
							core::Err{ r->takeError() } };
					}
					core::Span<uint8> s = r->value();
					buff = core::CStr{ reinterpret_cast<const char*>(s.ptr), s.len };
					pos = 0;                          // you also never reset pos after refill!
				}
			}
		}

	};
} // namespace itorator
