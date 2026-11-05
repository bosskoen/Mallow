#include <fs/path.h>
#include <stl/string.h>


using namespace fs;
#define CHECK(pred)   \
	if (!(pred))      \
	{                 \
		return false; \
	}

static core::CStr parentOf(core::CStr in) { return Path{ in }.parent().toCStr(); }

static core::String popped(core::CStr in)
{
	PathBuffer b{ Path{in} };
	b.pop();
	return core::String{ b.toCStr() };
}

static core::String poppedN(core::CStr in, int n)
{
	PathBuffer b{ Path{in} };
	for (int i = 0; i < n; ++i)
		b.pop();
	return core::String{ b.toCStr() };
}

static core::String pushed(core::CStr base, core::CStr tail)
{
	PathBuffer b{ Path{base} };
	b.push(Path{ tail });
	return core::String{ b.toCStr() };
}
static core::String normalized(core::CStr in)
{
	PathBuffer b{ Path{in} };
	b.normalize();
	return core::String{ b.toCStr() };
}
static core::String portable(core::CStr in)
{
	PathBuffer b{ Path{in} };
	b.toPortable();
	return core::String{ b.toCStr() };
}

static bool relIs(core::CStr from, core::CStr to, core::CStr expected)
{
	auto r = relative(Path{ from }, Path{ to });
	if (!r.isSome())
		return false;
	return core::String(r->toCStr()) == expected;
}

static bool relNone(core::CStr from, core::CStr to)
{
	return relative(Path{ from }, Path{ to }).isNone();
}

namespace core_file_system_test
{
	bool test_separator()
	{
		CHECK(Path::isSep('/'));
		CHECK(!Path::isSep('a'));
		CHECK(!Path::isSep('.'));
		CHECK(!Path::isSep(':'));
		CHECK(!Path::isSep(' '));
		CHECK(!Path::isSep('\0'));
#ifdef MLW_WINDOWS
		CHECK(Path::isSep('\\'));
#else
		CHECK(!Path::isSep('\\'));
#endif
		return true;
	}

	bool test_file_name()
	{
		// common
		CHECK(Path("/a/b/c.txt").fileName() == "c.txt");
		CHECK(Path("c.txt").fileName() == "c.txt");
		CHECK(Path("a/b").fileName() == "b");
		CHECK(Path("/a/b").fileName() == "b");
		CHECK(Path("/a/b/").fileName() == "");
		CHECK(Path("/").fileName() == "");
		CHECK(Path("").fileName() == "");
		CHECK(Path(".").fileName() == ".");
		CHECK(Path("a/..").fileName() == "..");

#ifdef MLW_WINDOWS
		CHECK(Path("a\\b\\c").fileName() == "c");
		CHECK(Path("a\\b/c").fileName() == "c");  // mixed separators
		CHECK(Path("C:\\a\\b.txt").fileName() == "b.txt");
		CHECK(Path("C:\\").fileName() == "");     // drive root
		CHECK(Path("C:").fileName() == "");       // bare drive
		CHECK(Path("C:b.txt").fileName() == "b.txt"); // drive-relative
		CHECK(Path("\\x").fileName() == "x");     // rooted
		CHECK(Path("\\\\srv\\share").fileName() == "");       // UNC root, share is not a file
		CHECK(Path("\\\\srv\\share\\").fileName() == "");
		CHECK(Path("\\\\srv\\share\\f.txt").fileName() == "f.txt");
		CHECK(Path("//srv/share/f.txt").fileName() == "f.txt");
#else
		CHECK(Path("a\\b").fileName() == "a\\b"); // backslash is a normal char
		CHECK(Path("C:").fileName() == "C:");     // no drives on POSIX
#endif
		return true;
	}

	bool test_parent()
	{
		// common
		CHECK(parentOf("/a/b/c") == "/a/b");
		CHECK(parentOf("/a/b/c/") == "/a/b");
		CHECK(parentOf("/a") == "/");
		CHECK(parentOf("/") == "/");
		CHECK(parentOf("a/b") == "a");
		CHECK(parentOf("a/b/") == "a");
		CHECK(parentOf("a") == "");
		CHECK(parentOf("") == "");

#ifdef MLW_WINDOWS
		// drive-absolute
		CHECK(parentOf("C:\\a\\b") == "C:\\a");
		CHECK(parentOf("C:\\a") == "C:\\");
		CHECK(parentOf("C:\\") == "C:\\");
		CHECK(parentOf("C:/a") == "C:/");
		// drive-relative
		CHECK(parentOf("C:a\\b") == "C:a");
		CHECK(parentOf("C:a") == "C:");
		CHECK(parentOf("C:") == "C:");
		// rooted
		CHECK(parentOf("\\a\\b") == "\\a");
		CHECK(parentOf("\\a") == "\\");
		CHECK(parentOf("\\") == "\\");
		// UNC
		CHECK(parentOf("\\\\srv\\share\\a\\b") == "\\\\srv\\share\\a");
		CHECK(parentOf("\\\\srv\\share\\a") == "\\\\srv\\share\\");
		CHECK(parentOf("\\\\srv\\share\\") == "\\\\srv\\share\\");
		CHECK(parentOf("\\\\srv\\share") == "\\\\srv\\share");
		// relative
		CHECK(parentOf("a\\b") == "a");
#endif
		return true;
	}

	//bool test_parent_matches_pop()
	//{
	//	const char* cases[] = {
	//		"/a/b/c", "/a/b/c/", "/a", "/", "a/b", "a", "",
	//#ifdef MLW_WINDOWS
	//		"C:\\a\\b", "C:\\a", "C:\\", "C:a\\b", "C:a", "C:",
	//		"\\a", "\\", "\\\\srv\\share\\a", "\\\\srv\\share\\", "\\\\srv\\share",
	//#endif
	//	};
	//	for (const char* c : cases) {
	//		CHECK(parentOf(c) == popped(c));
	//	}
	//	return true;
	//}

	bool test_extension()
	{
		CHECK(Path("/a/b/c.txt").extension() == "txt");
		CHECK(Path("archive.tar.gz").extension() == "gz"); // last dot wins
		CHECK(Path("a..b").extension() == "b");
		CHECK(Path("README").extension() == "");
		CHECK(Path(".gitignore").extension() == "");       // leading dot is not an ext
		CHECK(Path(".config.json").extension() == "json");
		CHECK(Path("/a/.config/x").extension() == "");     // dot in a dir
		CHECK(Path("/a/b.d/c").extension() == "");
		CHECK(Path("a.").extension() == "");               // trailing dot
		CHECK(Path(".").extension() == "");
		CHECK(Path("..").extension() == "");
		CHECK(Path("a.b/").extension() == "");             // trailing sep -> no file name
		CHECK(Path("").extension() == "");

#ifdef MLW_WINDOWS
		CHECK(Path("C:\\dir.d\\file").extension() == "");
		CHECK(Path("C:\\dir\\file.txt").extension() == "txt");
		CHECK(Path("C:file.txt").extension() == "txt");    // drive-relative
		CHECK(Path("\\\\srv\\share.x").extension() == ""); // share is root, not a file
		CHECK(Path("\\\\srv\\share\\f.md").extension() == "md");
#endif
		return true;
	}

	bool test_stem()
	{
		CHECK(Path("/a/b/c.txt").stem() == "c");
		CHECK(Path("archive.tar.gz").stem() == "archive.tar"); // only last ext stripped
		CHECK(Path("a..b").stem() == "a.");
		CHECK(Path("README").stem() == "README");
		CHECK(Path(".gitignore").stem() == ".gitignore");
		CHECK(Path(".config.json").stem() == ".config");
		CHECK(Path("a.").stem() == "a.");                      // ext empty -> stem == fileName
		CHECK(Path(".").stem() == ".");
		CHECK(Path("..").stem() == "..");
		CHECK(Path("/a/b/").stem() == "");
		CHECK(Path("").stem() == "");

#ifdef MLW_WINDOWS
		CHECK(Path("C:\\dir\\file.txt").stem() == "file");
		CHECK(Path("C:file.txt").stem() == "file");
		CHECK(Path("C:\\").stem() == "");
		CHECK(Path("\\\\srv\\share.x").stem() == "");
		CHECK(Path("\\\\srv\\share\\f.md").stem() == "f");
#endif
		return true;
	}

	bool test_absolute()
	{
		CHECK(!Path("").isAbsolute());
		CHECK(!Path("a/b").isAbsolute());
		CHECK(!Path("./a").isAbsolute());
		CHECK(!Path("../a").isAbsolute());

#ifndef MLW_WINDOWS
		CHECK(Path("/a/b").isAbsolute());
		CHECK(Path("/").isAbsolute());
		CHECK(!Path("C:/x").isAbsolute());  // no drives on POSIX
		CHECK(!Path("\\x").isAbsolute());   // backslash is a normal char
#else
		// drive-absolute
		CHECK(Path("C:\\x").isAbsolute());
		CHECK(Path("C:/x").isAbsolute());
		CHECK(Path("C:\\").isAbsolute());
		CHECK(Path("c:\\x").isAbsolute());  // lowercase drive
		// UNC
		CHECK(Path("\\\\srv\\share").isAbsolute());
		CHECK(Path("\\\\srv\\share\\x").isAbsolute());
		CHECK(Path("//srv/share/x").isAbsolute());
		// NOT absolute
		CHECK(!Path("C:").isAbsolute());    // bare drive
		CHECK(!Path("C:x").isAbsolute());   // drive-relative
		CHECK(!Path("\\x").isAbsolute());   // rooted, no drive
		CHECK(!Path("/x").isAbsolute());    // same as above with '/'
		CHECK(!Path("a\\b").isAbsolute());
#endif
		return true;
	}

	bool test_push()
	{
		// common
		CHECK(pushed("/a/b", "c") == "/a/b/c");
		CHECK(pushed("/a/b/", "c") == "/a/b/c");  // no double separator
		CHECK(pushed("/", "a") == "/a");
		CHECK(pushed("", "a") == "a");            // no leading separator added
		CHECK(pushed("/a", "") == "/a");          // empty tail -> unchanged
		CHECK(pushed("", "") == "");
		CHECK(pushed("a/b", "c/d") == "a/b/c/d");
		CHECK(pushed("a/", "../b") == "a/../b");  // push does not normalize

#ifndef MLW_WINDOWS
		CHECK(pushed("a", "b") == "a/b");         // native separator
		CHECK(pushed("/a/b", "/x") == "/x");      // absolute tail replaces
		CHECK(pushed("/a", "b\\c") == "/a/b\\c"); // backslash is a normal char
#else
		//CHECK(pushed("a", "b") == "a\\b");        // native separator
		//CHECK(pushed("C:\\a\\b", "c") == "C:\\a\\b\\c");
		//CHECK(pushed("C:/a", "c") == "C:/a/c");   // reuses base's separator style
		//CHECK(pushed("C:\\", "a") == "C:\\a");
		//CHECK(pushed("C:", "a") == "C:a");        // no separator after bare drive
		//CHECK(pushed("C:a", "b") == "C:a\\b");

		// absolute / prefixed tail replaces base
		CHECK(pushed("C:\\a\\b", "C:\\x") == "C:\\x");
		CHECK(pushed("C:\\a\\b", "D:\\x") == "D:\\x");
		CHECK(pushed("C:\\a", "D:x") == "D:x");
		CHECK(pushed("C:\\a", "\\\\srv\\share\\x") == "\\\\srv\\share\\x");
		CHECK(pushed("\\\\srv\\share\\a", "C:\\x") == "C:\\x");

		// rooted tail keeps base's prefix
		CHECK(pushed("C:\\a\\b", "\\x") == "C:\\x");
		CHECK(pushed("C:a", "\\x") == "C:\\x");
		CHECK(pushed("\\\\srv\\share\\a\\b", "\\x") == "\\\\srv\\share\\x");
		CHECK(pushed("a\\b", "\\x") == "\\x");    // no prefix to keep
		CHECK(pushed("/a", "/x") == "/x");

		// UNC base, relative tail
		CHECK(pushed("\\\\srv\\share", "a") == "\\\\srv\\share/a");
		CHECK(pushed("\\\\srv\\share\\", "a") == "\\\\srv\\share\\a");
#endif
		return true;
	}

	bool test_pop()
	{
		// common
		CHECK(popped("/a/b/c") == "/a/b");
		CHECK(popped("/a/b/") == "/a");           // trailing sep handled
		CHECK(popped("/a") == "/");
		CHECK(popped("/") == "/");                // root is the ceiling
		CHECK(popped("a/b") == "a");
		CHECK(popped("a") == "");
		CHECK(popped("") == "");
		CHECK(poppedN("/a/b", 2) == "/");
		CHECK(poppedN("/a/b", 5) == "/");         // repeated pops stop at root
		CHECK(poppedN("a/b", 5) == "");

#ifdef MLW_WINDOWS
		// drive-absolute
		CHECK(popped("C:\\a\\b\\c") == "C:\\a\\b");
		CHECK(popped("C:\\a\\b\\") == "C:\\a");
		CHECK(popped("C:\\a") == "C:\\");         // down to drive root, not "C:"
		CHECK(popped("C:\\") == "C:\\");
		CHECK(poppedN("C:\\a\\b", 5) == "C:\\");
		// drive-relative
		CHECK(popped("C:a\\b") == "C:a");
		CHECK(popped("C:a") == "C:");
		CHECK(popped("C:") == "C:");
		// rooted
		CHECK(popped("\\a\\b") == "\\a");
		CHECK(popped("\\a") == "\\");
		CHECK(popped("\\") == "\\");
		// UNC
		CHECK(popped("\\\\srv\\share\\a\\b") == "\\\\srv\\share\\a");
		CHECK(popped("\\\\srv\\share\\a") == "\\\\srv\\share\\");
		CHECK(popped("\\\\srv\\share\\") == "\\\\srv\\share\\");
		CHECK(popped("\\\\srv\\share") == "\\\\srv\\share");
		CHECK(poppedN("\\\\srv\\share\\a\\b\\c", 10) == "\\\\srv\\share\\");
#endif
		return true;
	}

	bool test_to_portable()
	{
		CHECK(portable("a/b/c") == "a/b/c");      // already portable
		CHECK(portable("") == "");

#ifdef MLW_WINDOWS
		CHECK(portable("a\\b\\c") == "a/b/c");
		CHECK(portable("a\\b/c") == "a/b/c");     // mixed
		CHECK(portable("C:\\a\\b") == "C:/a/b");
		CHECK(portable("\\\\srv\\share\\x") == "//srv/share/x");
		CHECK(portable("\\x") == "/x");
#else
		CHECK(portable("a\\b") == "a\\b");        // noop: backslash is a real char
#endif
		return true;
	}

#define NORMAL_TEST(in, expected) CHECK(normalized(in) == (expected));

	bool test_normalize()
	{
		// common: relative
		NORMAL_TEST("a/./b", "a/b")
			NORMAL_TEST("a//b", "a/b")
			NORMAL_TEST("a///b", "a/b")
			NORMAL_TEST("a/b/../c", "a/c")
			NORMAL_TEST("a/b/..", "a")
			NORMAL_TEST("a/b/.", "a/b")
			NORMAL_TEST("a/", "a")
			NORMAL_TEST("./a", "a")
			NORMAL_TEST("a/./b/./c", "a/b/c")
			NORMAL_TEST("a/../b/../c", "c")
			NORMAL_TEST("a/..", ".")
			NORMAL_TEST(".", ".")
			NORMAL_TEST("./", ".")
			NORMAL_TEST("", ".")
			NORMAL_TEST("..", "..")
			NORMAL_TEST("../a", "../a")
			NORMAL_TEST("../../a", "../../a")
			NORMAL_TEST("a/../../b", "../b")
			NORMAL_TEST("a/b/../../..", "..")

			// common: absolute
			NORMAL_TEST("/a/b", "/a/b")
			NORMAL_TEST("/a/", "/a")
			NORMAL_TEST("/", "/")
			NORMAL_TEST("/.", "/")
			NORMAL_TEST("/..", "/")
			NORMAL_TEST("/../a", "/a")
			NORMAL_TEST("/a/../../b", "/b")
			NORMAL_TEST("/a/b/../../..", "/")

#ifndef MLW_WINDOWS
			NORMAL_TEST("//a", "/a")
			NORMAL_TEST("a\\..\\b", "a\\..\\b")       // backslash is a normal char
#else
			// drive-absolute
			NORMAL_TEST("C:/a/../b", "C:/b")
			NORMAL_TEST("C:\\a\\..\\b", "C:\\b")
			NORMAL_TEST("C:\\a\\.\\b\\", "C:\\a/b")
			NORMAL_TEST("C:\\..\\a", "C:\\a")         // can't go above drive root
			NORMAL_TEST("C:\\..", "C:\\")
			NORMAL_TEST("C:\\", "C:\\")
			NORMAL_TEST("C:\\\\a", "C:\\a")

			// drive-relative
			NORMAL_TEST("C:foo/bar", "C:foo/bar")
			NORMAL_TEST("C:a\\.\\b", "C:a/b")
			NORMAL_TEST("C:a\\..\\..\\b", "C:../b")  // relative, so ".." is kept
			NORMAL_TEST("C:a\\..", "C:")
			NORMAL_TEST("C:", "C:")

			// rooted
			NORMAL_TEST("\\a\\..\\..\\b", "\\b")
			NORMAL_TEST("\\", "\\")

			// UNC: "\\srv\share" is the root and is never collapsed or popped
			NORMAL_TEST("\\\\srv\\share\\a\\..\\b", "\\\\srv\\share\\b")
			NORMAL_TEST("\\\\srv\\share\\..\\x", "\\\\srv\\share\\x")
			NORMAL_TEST("\\\\srv\\share\\\\x", "\\\\srv\\share\\x")
			NORMAL_TEST("\\\\srv\\share\\a\\", "\\\\srv\\share\\a")
			NORMAL_TEST("//srv/share/a/./b", "//srv/share/a/b")

			// relative with backslashes
			NORMAL_TEST("a\\.\\b", "a/b")
			NORMAL_TEST("a\\..\\..\\b", "../b")
#endif
			return true;
	}

	bool test_relative()
	{
		// --- absolute, common ---
		CHECK(relIs("/a/b", "/a/b", "."));
		CHECK(relIs("/", "/", "."));
		CHECK(relIs("/", "/a", "a"));
		CHECK(relIs("/a/b/", "/a/b/c", "c"));      // trailing separator ignored

		// --- relative, common ---
		CHECK(relIs("a", "a", "."));
		CHECK(relIs("", "a", "a"));                // "" is "."
		CHECK(relIs(".", "a", "a"));
		CHECK(relIs("../a", "../a/b", "b"));

		// --- impossible, common ---
		CHECK(relNone("../a", "b"));               // name of ".." is unknown
		CHECK(relNone("..", "a"));
		CHECK(relNone("../../a", "../b"));

#ifndef MLW_WINDOWS
		CHECK(relIs("/a/b", "/a/b/c/d", "c/d"));
		CHECK(relIs("/a/./b", "/a/b/../c", "../c")); // inputs normalized first
		CHECK(relIs("/a/b/c", "/a/b", ".."));
		CHECK(relIs("/a/b/c", "/a/d", "../../d"));
		CHECK(relIs("/a", "/", ".."));
		CHECK(relIs("/a/b", "/c/d", "../../c/d"));
		CHECK(relIs("/ab", "/a", "../a"));         // component-wise, not string prefix
		CHECK(relIs("/a", "/ab", "../ab"));
		CHECK(relIs("a/b", "a/c", "../c"));
		CHECK(relIs("a", "", ".."));
		CHECK(relIs("a", "../b", "../../b"));
		CHECK(relIs("../a", "../b", "../b"));
		CHECK(relIs("/a\\b", "/a\\b/c", "c"));     // backslash is a normal char

		CHECK(relNone("/a", "a"));                 // absolute vs relative
		CHECK(relNone("a", "/a"));
#else
		// drive-absolute
		CHECK(relIs("C:\\a\\b", "C:\\a\\b", "."));
		CHECK(relIs("C:\\a\\b", "C:\\a\\b\\c\\d", "c/d"));
		CHECK(relIs("C:\\a\\.\\b", "C:\\a\\b\\..\\c", "../c")); // inputs normalized first
		CHECK(relIs("C:\\a\\b\\c", "C:\\a\\b", ".."));
		CHECK(relIs("C:\\a\\b\\c", "C:\\a\\d", "../../d"));
		CHECK(relIs("C:\\", "C:\\a", "a"));
		CHECK(relIs("C:\\a", "C:\\", ".."));
		CHECK(relIs("C:\\ab", "C:\\a", "../a"));
		CHECK(relIs("C:/a", "C:\\a\\b", "b"));     // mixed separators
		CHECK(relIs("C:\\a", "c:\\a\\b", "b"));    // drive letter case-insensitive
		CHECK(relNone("C:\\a", "D:\\a"));          // different drives

		// drive-relative
		CHECK(relIs("C:a", "C:b", "../b"));
		CHECK(relIs("C:", "C:a", "a"));
		CHECK(relNone("C:a", "D:a"));
		CHECK(relNone("C:a", "C:/a"));            // drive-relative vs absolute
		CHECK(relNone("C:a", "a"));                // drive-relative vs plain relative

		// rooted
		CHECK(relIs("\\a", "\\b", "../b"));
		CHECK(relIs("\\", "\\a\\b", "a/b"));
		CHECK(relNone("\\a", "C:\\a"));            // rooted vs drive-absolute
		CHECK(relNone("\\a", "a"));

		// UNC
		CHECK(relIs("\\\\srv\\share\\a", "\\\\srv\\share\\b\\c", "../b/c"));
		CHECK(relIs("\\\\srv\\share", "\\\\srv\\share\\a", "a"));
		CHECK(relIs("\\\\srv\\share\\a", "\\\\srv\\share", ".."));
		CHECK(relIs("//srv/share/a", "\\\\srv\\share\\a\\b", "b"));
		CHECK(relNone("\\\\srv\\s1\\a", "\\\\srv\\s2\\a"));   // different share
		CHECK(relNone("\\\\srv1\\s\\a", "\\\\srv2\\s\\a"));   // different server
		CHECK(relNone("\\\\srv\\share\\a", "C:\\a"));

		// plain relative
		CHECK(relIs("a\\b", "a\\c", "../c"));
		CHECK(relIs("a", "", ".."));
		CHECK(relIs("a", "..\\b", "../../b"));
		CHECK(relIs("..\\a", "..\\b", "../b"));

		CHECK(relNone("C:\\a", "a"));              // absolute vs relative
		CHECK(relNone("a", "C:\\a"));
#endif
		return true;
	}

	// Property: pushing the result onto `from` and normalizing gives `to`.
	bool test_relative_roundtrip()
	{
		struct Case { core::CStr from; core::CStr to; };
		const Case cases[] = {
			{"/a/b/c", "/a/d/e"}, {"/", "/x/y"}, {"/x/y", "/"},
			{"a/b", "a/c/d"}, {"a", "../b"}, {"", "a/b"},
	#ifdef MLW_WINDOWS
			{"C:\\a\\b", "C:\\c"}, {"C:a\\b", "C:c"}, {"\\a\\b", "\\c"},
			{"\\\\srv\\share\\a\\b", "\\\\srv\\share\\c"},
	#endif
		};
		for (const Case& c : cases)
		{
			auto r = relative(Path{ c.from }, Path{ c.to });
			CHECK(r.isSome());
			PathBuffer b{ Path{c.from} };
			b.push(Path{ r->toCStr() });
			b.normalize();
			b.toPortable(); // push may mix separator styles; compare portably
			PathBuffer expected{ Path{c.to} };
			expected.normalize();
			expected.toPortable();
			CHECK(b.toCStr() == expected.toCStr());
		}
		return true;
	}


} // namespace core_file_system_test
