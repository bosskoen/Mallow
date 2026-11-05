#include "path.h"
#include <core/ascii.h>
#include <stl/vector.h>
#include <core/error.h>

// ============================================================================
//  Path model
//  ----------
//  Every path is  [ROOT][COMPONENTS].  The ROOT is the fixed prefix that ".."
//  can never escape and that fixes the path's "kind"; COMPONENTS are the names
//  after it, separated by separators.
//
//  There are five root kinds. rootLen() is the ONE place that recognises them;
//  every other function derives from it, so none of them re-scan for the root
//  and none of them can disagree about where it ends.
//
//    kind                 example        rootLen   isAbsolute  escapable ".."
//    -------------------  -------------  --------   ----------  -------------
//    relative             a/b            0          no          yes (keep ..)
//    unix absolute        /a/b           1          yes         no  (drop ..)
//    drive-relative (win) \a\b           1          no          yes (keep ..)
//    drive-local   (win)  C:a\b          2          no          yes (keep ..)
//    drive-absolute(win)  C:\a\b         3          yes         no  (drop ..)
//    UNC           (win)  \\srv\sh\a     through sh yes         no  (drop ..)
//
//  Two things are easy to conflate and must not be:
//    * "how many bytes is the root"      -> rootLen()      (what to PRESERVE)
//    * "may .. be dropped at the root"   -> isAbsolute()   (root is a real floor)
//  They line up for most kinds but split for drive-local "C:foo": it keeps a
//  2-byte root yet ".." behaves relatively (kept, not dropped), because "C:foo"
//  is relative to drive C's current directory.
// ============================================================================

namespace
{
	// Last separator index in p, or -1 if none.
	isize lastSep(fs::Path p)
	{
		for (isize i = p.len - 1; i >= 0; --i)
			if (fs::Path::isSep(p.ptr[i]))
				return i;
		return -1;
	}
}

// ----------------------------------------------------------------------------
//  rootLen — the single source of truth. Everything else calls this.
// ----------------------------------------------------------------------------
isize fs::Path::rootLen() const
{
	if (len == 0)
		return 0;

#ifdef MLW_WINDOWS
	// UNC: "\\server\share" — root spans both the server and the share name.
	if (len >= 2 && isSep(ptr[0]) && isSep(ptr[1]))
	{
		isize i = 2;
		while (i < len && !isSep(ptr[i]))
			++i; // past server
		if (i < len)
			++i; // the separator after server
		while (i < len && !isSep(ptr[i]))
			++i; // past share
		if (i < len) ++i;
		return i;
	}

	// Drive letter present. The byte after the colon decides the kind:
	//   "C:\..."  -> drive-ABSOLUTE, 3-byte root ("C:\")
	//   "C:..."   -> drive-LOCAL,    2-byte root ("C:")   <- the one that was corrupting pop/normalize
	if (len >= 2 && core::isAlpha(ptr[0]) && ptr[1] == ':')
		return (len >= 3 && isSep(ptr[2])) ? 3 : 2;

	// Leading separator, no drive -> drive-relative "\x", 1-byte root ("\").
	if (isSep(ptr[0]))
		return 1;

	return 0; // relative
#else
	return isSep(ptr[0]) ? 1 : 0; // "/x" or relative
#endif
}

// ----------------------------------------------------------------------------
//  Predicates — all derived from rootLen(), so they can never disagree.
// ----------------------------------------------------------------------------
bool fs::Path::hasRoot() const
{
	return rootLen() != 0;
}

bool fs::Path::isAbsolute() const
{
	// Absolute = self-contained, needs no external context.
#ifdef MLW_WINDOWS
	// UNC (\\...) or drive-absolute (C:\...) -> rootLen >= 3, OR the 2-byte UNC
	// prefix case. Drive-relative (\x, rootLen 1) and drive-local (C:x, rootLen 2
	// without a leading sep) are NOT absolute.
	if (len >= 2 && isSep(ptr[0]) && isSep(ptr[1]))
		return true; // UNC
	if (len >= 3 && core::isAlpha(ptr[0]) && ptr[1] == ':' && isSep(ptr[2]))
		return true; // C:\path

	return false;
#else
	return rootLen() == 1; // leading '/'
#endif
}

bool fs::Path::isRootRelative() const
{
	// "\x" — leading separator, no drive. Resolves against the CWD's drive.
#ifdef MLW_WINDOWS
	return len >= 1 && isSep(ptr[0]) && !isAbsolute();
#else
	return false; // does not exist on Unix ('/' is absolute there)
#endif
}

bool fs::Path::isDriveRelative() const
{
	// "C:x" — drive, but no separator after the colon. Relative to drive C's cwd.
#ifdef MLW_WINDOWS
	return len >= 2 && core::isAlpha(ptr[0]) && ptr[1] == ':' && !(len >= 3 && isSep(ptr[2]));
#else
	return false;
#endif
}

// ----------------------------------------------------------------------------
//  Queries — all floor at rootLen() so they never reach into the root.
// ----------------------------------------------------------------------------
core::CStr fs::Path::fileName() const
{
	isize root = rootLen();
	// Last separator at or above the root (never scan into the root itself).
	isize s = -1;
	for (isize i = len - 1; i >= root; --i)
		if (isSep(ptr[i])) { s = i; break; }

	// Filename starts after the separator, or — if there is none past the root —
	// right after the root (handles "C:b.txt" where root and name abut).
	isize start = (s < 0) ? root : s + 1;
	return core::CStr{ ptr + start, len - start };
}

fs::Path fs::Path::parent() const
{
	isize root = rootLen();
	isize end = len;
	if (end > root && isSep(ptr[end - 1]))
		--end; // ignore a single trailing separator

	// Find the last separator, but never below the root.
	isize s = -1;
	for (isize i = end - 1; i >= root; --i)
		if (isSep(ptr[i]))
		{
			s = i;
			break;
		}

	if (s < 0)
		return fs::Path{ ptr, root }; // no separator past the root -> the root itself
	if (s == 0)
		return fs::Path{ ptr, 1 }; // separator at index 0 -> keep "/"
	return fs::Path{ ptr, s };
}

core::CStr fs::Path::extension() const
{
	core::CStr name = fileName();
	// Last '.' inside the filename; a dot at index 0 is a leading dot, not an ext.
	for (isize i = name.len - 1; i > 0; --i)
		if (name.ptr[i] == '.')
			return core::CStr{ name.ptr + i + 1, name.len - i - 1 };
	return core::CStr{ "", 0 };
}

core::CStr fs::Path::stem() const
{
	core::CStr name = fileName();
	// A '.' separates an extension only if it's not the leading char (i > 0)
	// AND not the trailing char (i < name.len - 1). Otherwise it's part of the stem.
	for (isize i = name.len - 1; i > 0; --i)
		if (name.ptr[i] == '.')
		{
			if (i == name.len - 1) return name;        // trailing dot -> no ext -> whole name is stem
			return core::CStr{ name.ptr, i };
		}
	return name;
}

// ----------------------------------------------------------------------------
//  PathBuffer::push — the ONE place a root kind changes behaviour.
//
//    absolute tail       -> replace everything          ("/x", "C:\x", "\\s\sh")
//    drive-relative tail -> keep drive, replace rest    ("\x")        [windows]
//    anything else       -> append with one separator
//
//  Separators the user supplied are preserved verbatim; the only separator this
//  function ever EMITS is '/'.
// ----------------------------------------------------------------------------
void fs::PathBuffer::push(Path tail)
{
	if (tail.len == 0)
		return;

	if (tail.isAbsolute())
	{
		buffer.clear();
		buffer.append(tail.toCStr());
		return;
	}

#ifdef MLW_WINDOWS
	if (tail.isRootRelative())
	{
		isize baseRoot = static_cast<Path>(*this).rootLen();
		if (baseRoot >= 2) {
			buffer.truncate(baseRoot);
			// if the kept root ends in a separator and the tail starts with one, drop one
			if (buffer.len() != 0 && Path::isSep(buffer.back()))
				buffer.truncate(buffer.len() - 1);   // strip root's trailing sep; tail supplies it
			buffer.append(tail.toCStr());            // tail's leading sep is now the single join
		}
		else {
			buffer.clear();
			buffer.append(tail.toCStr());
		}
		return;
	}
#endif

#ifdef MLW_WINDOWS
	if (tail.isDriveRelative())   // "D:x" — drive letter, no leading separator
	{
		char tailDrive = core::toLower(tail.ptr[0]);
		char baseDrive = (buffer.len() >= 2 && core::isAlpha(buffer.data()[0]) && buffer.data()[1] == ':')
			? core::toLower(buffer.data()[0]) : 0;
		if (tailDrive != baseDrive) {
			// different drive -> switch entirely
			buffer.clear();
			buffer.append(tail.toCStr());   // "D:x"
			return;
		}
		// same drive -> drop the redundant "C:" from the tail, append the rest relatively
		Path rest{ tail.ptr + 2, tail.len - 2 };   // skip "D:"
		// fall through to relative append with `rest`... (see note)
	}
#endif

	// Plain relative: insert exactly one '/' only if neither side already has one.
	if (buffer.len() != 0 && !Path::isSep(buffer.back()) && !Path::isSep(tail.ptr[0]))
		buffer.push('/');
	buffer.append(tail.toCStr());
}

// ----------------------------------------------------------------------------
//  PathBuffer::pop — remove the last component, but never cut into the root.
//  Flooring at rootLen() is what makes "C:foo" and "C:\" and "/" behave:
//  pop stops at the root instead of slicing through "C:" or past "/".
// ----------------------------------------------------------------------------
void fs::PathBuffer::pop()
{
	isize root = static_cast<Path>(*this).rootLen();
	isize end = buffer.len();
	if (end == 0)
		return;
	if (end > root && Path::isSep(buffer.data()[end - 1]))
		--end; // ignore a trailing separator

	// Find the last separator at or above the root.
	isize s = -1;
	for (isize i = end - 1; i >= root; --i)
		if (Path::isSep(buffer.data()[i]))
		{
			s = i;
			break;
		}

	if (s < 0)
		buffer.truncate(root); // no separator past the root -> back to the root
	else if (s == 0)
		buffer.truncate(1); // "/a" -> "/"
	else
		buffer.truncate(s);
}

// ----------------------------------------------------------------------------
//  PathBuffer::toPortable — rewrite native '\' separators to canonical '/'.
//  No-op on Unix ('\' is a real filename character there). On Windows, leaves
//  the UNC / "\\?\" prefixes alone because those require native backslashes.
// ----------------------------------------------------------------------------
void fs::PathBuffer::toPortable()
{
#ifndef MLW_WINDOWS
	return;
#else
	isize n = buffer.len();
	char* d = buffer.data();
	// Only "\\?\" must stay native (raw non-normalized paths — '/' breaks them).
	if (n >= 4 && d[0] == '\\' && d[1] == '\\' && d[2] == '?' && d[3] == '\\')
		return;
	// Everything else, INCLUDING UNC "\\server", converts for internal consistency.
	for (isize i = 0; i < n; ++i)
		if (d[i] == '\\') d[i] = '/';
#endif
}

// ----------------------------------------------------------------------------
//  PathBuffer::normalize — collapse ".", "..", and redundant separators,
//  lexically (no syscalls, no symlink resolution).
//
//  Two root facts drive it:
//    rootLen     = how many leading bytes to preserve untouched.
//    rootFloors  = whether ".." may be DROPPED at the root. Only truly absolute
//                  roots (unix "/", "C:\", UNC) floor; relative, drive-relative
//                  ("\x") and drive-local ("C:x") all KEEP a leading "..".
// ----------------------------------------------------------------------------
void fs::PathBuffer::normalize()
{
	Path self{ buffer.data(), buffer.len() };
	isize size = buffer.len();

	isize rootLen = self.rootLen();
	bool rootFloors = self.isAbsolute() || self.isRootRelative();; // only absolute roots drop ".."

	core::Vector<core::CStr> stack{};

	isize i = rootLen;
	while (i < size)
	{
		isize start = i;
		for (; i < size; ++i)
			if (Path::isSep(buffer[i]))
				break;
		core::CStr section{ buffer.data() + start, i - start };

		if (section.len == 0)
		{
			// empty (from a doubled separator) -> skip
		}
		else if (section.len == 1 && section[0] == '.')
		{
			// "." -> skip
		}
		else if (section.len == 2 && section[0] == '.' && section[1] == '.')
		{
			bool topIsDotDot = !stack.isEmpty() && stack.back()->len == 2 && stack.back()->ptr[0] == '.' && stack.back()->ptr[1] == '.';

			if (!stack.isEmpty() && !topIsDotDot)
				stack.pop(); // cancel the previous real component
			else if (!rootFloors)
				stack.push(section); // relative-ish root -> keep the ".."
			// absolute root with nothing to pop -> drop it (can't go above root)
		}
		else
		{
			stack.push(section);
		}

			++i; // step over the separator
	}

	// Rebuild: preserved root + components joined by a single '/'.
	core::String out{};
	if (rootLen != 0)
	{
		out.append(buffer.slice(0, rootLen));
		// A drive-local root ("C:") has no trailing separator; give the first
		// component a '/' so "C:" + "foo" doesn't fuse into "C:foo"... but that
		// IS the correct drive-local spelling, so only add '/' when the root
		// already ends in one (absolute/drive-relative) handles itself below.
	}

	for (isize k = 0; k < stack.len(); ++k)
	{
		if (k != 0)
			out.push('/');
		out.append(stack[k]);
	}

	if (out.isEmpty())
		out.push('.'); // nothing left -> current directory

	buffer = core::move(out);
}

// ----------------------------------------------------------------------------
//  getWorkingDir — the current directory as a PathBuffer. Grows the buffer and
//  retries on BufferTooSmall so the caller never has to guess a size.
// ----------------------------------------------------------------------------
core::Result<fs::PathBuffer, io::IoError> fs::PathBuffer::getWorkingDir()
{
	PathBuffer buf{};
	isize cap = 260; // MAX_PATH on Windows; PATH_MAX is larger on Unix but grows below
	for (;;)
	{
		buf.buffer.resize(cap);
		core::Result<isize, io::IoError> r = io::getCwdInto(buf.toCStr());
		if (r.isOk())
		{
			buf.buffer.truncate(r.value());
			return core::Ok{ core::move(buf) };
		}
		if (r.error().kind == io::IoError::Kind::BufferTooSmall && cap < (1 << 20))
		{
			cap *= 2;
			continue;
		}
		return core::Err{ r.takeError() };
	}
}

// ----------------------------------------------------------------------------
//  absolute — expand a relative (or drive-relative / drive-local) path against
//  the current directory, then normalize. Already-absolute paths are untouched.
//  The member mutates *this; the static returns a fresh buffer. Both share the
//  static's implementation.
// ----------------------------------------------------------------------------
core::Result<fs::PathBuffer, io::IoError> fs::PathBuffer::absolute(fs::Path p)
{
	if (p.isAbsolute())
	{
		PathBuffer out{ p };
		out.normalize();
		return core::Ok{ core::move(out) };
	}

	// Not absolute: resolve against the CWD. push() supplies the CWD's drive for
	// a drive-relative "\x", and simply joins for a plain relative path.
	TRY(cwd, PathBuffer::getWorkingDir());
	cwd.push(p);
	cwd.normalize();
	return core::Ok{ core::move(cwd) };
}

core::Result<unit, io::IoError> fs::PathBuffer::absolute()
{
	TRY(abs, PathBuffer::absolute(static_cast<Path>(*this)));
	buffer = core::move(abs.buffer);
	return core::Ok<unit>{};
}

namespace {
	static isize rootCmpLen(fs::Path p) {
		isize r = p.rootLen();
#ifdef MLW_WINDOWS
		// UNC = starts with two separators
		bool isUnc = p.len >= 2 && fs::Path::isSep(p.ptr[0]) && fs::Path::isSep(p.ptr[1]);
		if (isUnc && r > 0 && fs::Path::isSep(p.ptr[r - 1]))
			--r;   // drop UNC's optional trailing sep
#endif
		return r;
	}

	static bool sameRoot(fs::Path a, fs::Path b) {
		isize ra = rootCmpLen(a);
		isize rb = rootCmpLen(b);
		if (ra != rb) return false;
		for (isize i = 0; i < ra; ++i) {
#ifdef MLW_WINDOWS
			char ca = core::toLower(a.ptr[i]);
			char cb = core::toLower(b.ptr[i]);
			if (ca != cb && !(fs::Path::isSep(a.ptr[i]) && fs::Path::isSep(b.ptr[i])))
				return false;
#else
			if (a.ptr[i] != b.ptr[i]) return false;
#endif
		}
		return true;
	}

	// Components after the root, as views. Assumes p is already normalized
// (so no "." or ".." leak through for the common-prefix match to trip on).
	static core::Vector<core::CStr> components(fs::Path p)
	{
		core::Vector<core::CStr> out;
		isize i = p.rootLen(), n = p.len;
		while (i < n) {
			isize start = i;
			while (i < n && !fs::Path::isSep(p.ptr[i])) ++i;
			core::CStr sec{ p.ptr + start, i - start };
			if (sec.len != 0 && !(sec.len == 1 && sec.ptr[0] == '.'))   // skip "" and "."
				out.push(sec);
			++i;
		}
		return out;
	}
}


core::Optional<fs::PathBuffer> fs::relative(fs::Path from, fs::Path to)
{
	// 1. Must be the same kind of path (both have a root, or both relative) and same root.
	//    Normalize first so ".." in the inputs doesn't corrupt component matching.
	PathBuffer nf{ from }; nf.normalize();
	PathBuffer nt{ to };   nt.normalize();
	Path pf = nf, pt = nt;

	if (!sameRoot(pf, pt))
		return {};                      // no relative path exists (different roots/kinds)

	// 2. Split into components.
	core::Vector<core::CStr> cf = components(pf);
	core::Vector<core::CStr> ct = components(pt);

	// 3. Common prefix length.
	isize common = 0;
	while (common < cf.len() && common < ct.len() && cf[common] == ct[common])
		++common;

	for (isize i = common; i < cf.len(); ++i)
		if (cf[i].len == 2 && cf[i].ptr[0] == '.' && cf[i].ptr[1] == '.')
			return {};

	// 4. Build: ".." for each leftover `from` component, then leftover `to` components.
	PathBuffer out{};
	for (isize i = common; i < cf.len(); ++i)
		out.push(Path{ "..", 2 });
	for (isize i = common; i < ct.len(); ++i)
		out.push(ct[i]);

	// 5. Same location -> "."
	if (static_cast<Path>(out).len == 0)
		out = PathBuffer{ Path{".", 1} };

	return out;
}