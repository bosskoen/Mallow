#pragma once
#include <core/libc/mem.h>
#include <stl/string.h>
#include <core/io/io.h>
#include <core/io/format.h>

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

		bool hasRoot() const;
		isize rootLen() const;
        bool isRootRelative() const;
		bool isDriveRelative() const;
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


		// relative
	};

	class PathBuffer
	{
		core::String buffer{};
		PathBuffer(core::String&& s) : buffer(core::move(s)) {};

	public:
		PathBuffer() = default;
		explicit PathBuffer(core::CStr s) : buffer(s) {}
		explicit PathBuffer(Path p) : buffer(p.toCStr()) {}

		MLW_FORCE_INLINE PathBuffer clone()
		{
			return PathBuffer{ buffer.clone() };
		}
		operator Path() const { return Path{ buffer.data(), buffer.len() }; }
		MLW_FORCE_INLINE core::CStr toCStr() const { return buffer; }

		MLW_FORCE_INLINE void clear() { buffer.clear(); }
		MLW_FORCE_INLINE char* data() {return buffer.data();}
		MLW_FORCE_INLINE isize len() const {return buffer.len();}

		void push(Path tail);
		void pop();

		// noop on unix on window changes every "\\" with '/'
		void toPortable();

		void normalize();

		static core::Result<PathBuffer, io::IoError> getWorkingDir();
		static core::Result<PathBuffer, io::IoError> absolute(Path p);
		core::Result<unit, io::IoError> absolute();


		template <core::FormatBuffer Buffer>
		void format(Buffer &buffer) const { buffer.append(toCStr()); }
	};

	core::Optional<PathBuffer> relative(Path from, Path to);


} // namespace fs
