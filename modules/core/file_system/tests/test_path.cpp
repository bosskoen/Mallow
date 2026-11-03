#include <fs/path.h>

using namespace fs;
#define CHECK(pred)   \
    if (!(pred))      \
    {                 \
        return false; \
    }

namespace core_file_system_test
{
	bool test_separator()
	{
		CHECK(Path::isSep('/'))
			CHECK(!Path::isSep('a'))
			CHECK(!Path::isSep('.'))
#ifdef MLW_WINDOWS
			CHECK(Path::isSep('\\'));
#else
			CHECK(!Path::isSep('\\'));
#endif
		return true;
	}

	bool test_file_name()
	{
		CHECK(Path("/a/b/c.txt").fileName() == "c.txt");
		CHECK(Path("c.txt").fileName() == "c.txt"); // no separator -> whole thing
		CHECK(Path("/a/b/").fileName() == "");      // trailing sep -> empty
		CHECK(Path("/").fileName() == "");          // root -> empty
		CHECK(Path("").fileName() == "");           // empty -> empty
		CHECK(Path{ "/a/b" }.fileName() == "b");
		CHECK(Path{ "test.txt" }.fileName() == "test.txt");
#ifdef MLW_WINDOWS
		CHECK(Path("a\\b\\c").fileName() == "c"); // backslash counts on Windows
#endif
		return true;
	}

	bool test_parent()
	{
		CHECK(Path("/a/b/c").parent().toCStr() == "/a/b");
		CHECK(Path("/a/b/c/").parent().toCStr() == "/a/b"); // trailing sep ignored for finding last comp
		CHECK(Path("/a").parent().toCStr() == "/");         // parent of top-level is root... or "" ? DECIDE
		CHECK(Path("a").parent().toCStr() == "");           // no separator -> empty (Rust-style)
		CHECK(Path("/").parent().toCStr() == "/");          // root's parent is itself... or "" ? DECIDE
		CHECK(Path("").parent().toCStr() == "");
		return true;
	}

	bool test_extension()
	{
		CHECK(Path("/a/b/c.txt").extension() == "txt");
		CHECK(Path("archive.tar.gz").extension() == "gz"); // last dot wins
		CHECK(Path("README").extension() == "");           // no dot
		CHECK(Path(".gitignore").extension() == "");       // leading dot is NOT an extension
		CHECK(Path("/a/.config/x").extension() == "");     // dot in a dir, not the filename
		CHECK(Path("a.").extension() == "");               // trailing dot -> empty ext (DECIDE: or ""? yes empty)
		CHECK(Path("/a/b.d/c").extension() == "");
		return true;
	}

	bool test_stem()
	{
		CHECK(Path("/a/b/c.txt").stem() == "c");
		CHECK(Path("archive.tar.gz").stem() == "archive.tar"); // only the LAST ext stripped
		CHECK(Path("README").stem() == "README");              // no ext -> whole filename
		CHECK(Path(".gitignore").stem() == ".gitignore");      // leading dot stays with stem
		CHECK(Path("/a/b/").stem() == "");
		return true;
	}

	bool test_absolute()
	{
#ifndef MLW_WINDOWS
		CHECK(Path("/a/b").isAbsolute());
		CHECK(!Path("a/b").isAbsolute());
		CHECK(!Path("").isAbsolute());
#else
		CHECK(Path("C:\\x").isAbsolute());
		CHECK(Path("C:/x").isAbsolute());
		CHECK(Path("\\\\srv\\s").isAbsolute()); // UNC "\\srv\s"
		CHECK(!Path("a\\b").isAbsolute());
		CHECK(!Path("\\x").isAbsolute()); // DECIDE: leading single sep, no drive — "rooted" but not "absolute" on Windows
#endif
		return true;
	}

	bool test_push()
	{
		Pathbuffer p{ Path("/a/b") };
		p.push(Path("c"));
		CHECK(p.toCStr() == "/a/b/c");

		Pathbuffer q{ Path("/a/b/") };
		q.push(Path("c"));             // base ends in sep
		CHECK(q.toCStr() == "/a/b/c"); // no double slash

#ifndef MLW_WINDOWS
		Pathbuffer r{ Path("/a/b") };
		r.push(Path("/x"));
		CHECK(r.toCStr() == "/x");
#else
		Pathbuffer r{ Path("C:\\a\\b") };
		r.push(Path("C:\\x"));
		CHECK(r.toCStr() == "C:\\x");

		Pathbuffer x{ Path("C:\\a\\b") };
		x.push(Path("\\x"));
		CHECK(x.toCStr() == "C:\\x");
#endif

		Pathbuffer s{};
		s.push(Path("a"));        // empty base
		CHECK(s.toCStr() == "a"); // no leading sep added

		Pathbuffer t{ Path("/a") };
		t.push(Path("")); // empty tail
		CHECK(t.toCStr() == "/a");
		return true;
	}

	bool test_pop()
	{
		Pathbuffer p{ Path("/a/b/c") };
		p.pop();
		CHECK(p.toCStr() == "/a/b");
		Pathbuffer q{ Path("/a/b/") };
		q.pop();
		CHECK(q.toCStr() == "/a"); // trailing sep handled
		Pathbuffer r{ Path("/a") };
		r.pop();
		CHECK(r.toCStr() == "/"); // DECIDE: "/" or ""?
		Pathbuffer s{ Path("a") };
		s.pop();
		CHECK(s.toCStr() == ""); // no sep -> empty
		Pathbuffer t{ Path("/") };
		t.pop();
		CHECK(t.toCStr() == "/");

#ifdef MLW_WINDOWS
		Pathbuffer w{ Path("C:\\a\\b\\c") };
		w.pop();
		CHECK(w.toCStr() == "C:\\a\\b");
		Pathbuffer y{ Path("C:\\a\\b\\") };
		y.pop();
		CHECK(y.toCStr() == "C:\\a");
		Pathbuffer x{ Path("C:\\a") };
		x.pop();
		CHECK(x.toCStr() == "C:\\"); // down to drive root, NOT "C:"
		Pathbuffer z{ Path("C:\\") };
		z.pop();
		CHECK(z.toCStr() == "C:\\"); // drive root is the ceiling
#endif

		return true;
	}

	bool test_to_portable()
	{
#ifdef MLW_WINDOWS
		Pathbuffer p{ Path("a\\b\\c") };
		p.toPortable();
		CHECK(p.toCStr()== "a/b/c");
#else
		Pathbuffer p{ Path("a\\b") };
		p.toPortable();
		CHECK(p.toCStr()== "a\\b"); // noop: backslash is a real char
#endif
		return true;
	}
} // namespace core_file_system_test


