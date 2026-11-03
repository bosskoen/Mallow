#pragma once
#include <core/libc/mem.h>
#include <stl/string.h>

namespace fs
{
	struct Path
	{
		const char* ptr = nullptr; // declare ptr first (matches init order)
		isize len = 0;

		template <index_t N>
		MLW_FORCE_INLINE constexpr Path(const char(&str)[N]) : ptr(str), len(N - 1) {} // literals

		MLW_FORCE_INLINE constexpr Path(const char* p, isize l) : ptr(p), len(l) {} // explicit (buffers)

		MLW_FORCE_INLINE constexpr Path(core::CStr s) : ptr(s.ptr), len(s.len) {} // CStr -> Path (one hop)

		MLW_FORCE_INLINE static Path fromPtr(const char* p)
		{
			return Path(p, static_cast<isize>(core::mlwStrlen(p)));
		}

		MLW_FORCE_INLINE core::CStr toCStr() const { return core::CStr(ptr, len); }

		bool operator==(const Path& other) {

			if (len != other.len) return false;
			if (ptr == other.ptr) return true;
			return core::mlwMemcmp(ptr, other.ptr, len) == 0;
		}

        bool isRoot() const;
		bool isAbsolute() const;
		core::CStr fileName() const;
		Path parent() const;
		core::CStr extension() const;
		core::CStr stem() const;

		MLW_FORCE_INLINE static bool isSep(char c)
		{
			return c == '/'
#ifdef MLW_WINDOWS
				|| c == '\\'
#endif
				;
		}

		// normalize
		// relative
		// absolute
		// itorate
	};

	class Pathbuffer
	{
		core::String buffer{};
		Pathbuffer(core::String&& s) : buffer(core::move(s)) {};

	public:
		Pathbuffer() = default;
		explicit Pathbuffer(core::CStr s) : buffer(s) {}
		explicit Pathbuffer(Path p) : buffer(p.toCStr()) {}

		MLW_FORCE_INLINE Pathbuffer clone()
		{
			return Pathbuffer{ buffer.clone() };
		}
		operator Path() const { return Path{ buffer.data(), buffer.len() }; }
		MLW_FORCE_INLINE core::CStr toCStr() const { return buffer; }

		MLW_FORCE_INLINE void clear() { buffer.clear(); }

		void push(Path tail);
		void pop();

		// noop on unix on window changes every "\\" with '/'
		void toPortable();
	};

} // namespace fs
