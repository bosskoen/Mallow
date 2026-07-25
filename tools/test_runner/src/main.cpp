#include <string>
#include <vector>
#include <list>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ---- knobs: change these to match your entry / harness --------------------
// The symbol your entry's mlwStart calls into. The generated dispatcher defines it.
static const char *const ENTRY_SYMBOL = "mallowMain";
// A hand-written header declaring the reporting interface the dispatcher calls.
// Simplest home: a public header in core, so `tests` gets it by linking core::core.
static const char *const REPORT_HEADER = "core/macro.h";
// ---------------------------------------------------------------------------

struct Module
{
    std::string name;
    std::string ns;
    fs::path path;
    std::vector<std::string> functions;
};

static const char *const ROOT_DIR = ".";

void find_modules(const fs::path &path, std::list<Module> &mods)
{
    for (const fs::directory_entry &entry : fs::directory_iterator(path))
    {
        if (!entry.is_directory())
            continue;

        if (fs::exists(entry.path() / "CMakeLists.txt"))
        {
            Module m;
            m.path = entry.path();
            mods.push_back(std::move(m));
        }
        else
        {
            find_modules(entry.path(), mods);
        }
    }
}

void filter_test_modules(std::list<Module> &mods)
{
    auto it = mods.begin();
    while (it != mods.end())
    {
        if (!fs::exists(it->path / "tests" / "CMakeLists.txt"))
        {
            it = mods.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void path_to_name(Module &mod)
{
    mod.name = std::string{};
    mod.ns = std::string{};

    bool found_modules = false;
    for (auto &part : mod.path)
    {
        if (!found_modules)
        {
            if (part.string() == "modules")
                found_modules = true;
            continue;
        }
        mod.name = part.string();
        mod.ns += part.string();
        mod.ns += '_';
    }
    mod.ns += "test";
}

// returns difrense in {}
int get_indentement(const std::string &line)
{
    int x = 0;
    for (const char &c : line)
    {
        if (c == '{')
            ++x;
        else if (c == '}')
            --x;
    }
    return x;
}

void find_test_functions_in_dir(Module &mod, const fs::path &path)
{
    for (auto &entry : fs::directory_iterator(path))
    {
        if (entry.is_directory())
        {
            find_test_functions_in_dir(mod, entry.path()); // recurse
            continue;
        }
        if (entry.path().extension() != ".cpp")
            continue;

        std::ifstream file(entry.path());
        std::string line;
        bool in_namespace = false;
        int indentation = 0;

        while (std::getline(file, line))
        {
            size_t first = line.find_first_not_of(" \t");
            if (first == std::string::npos) continue;
            if (line.compare(first, 2, "//") == 0) continue;

            if (line.find("namespace") != std::string::npos && line.find(mod.ns) != std::string::npos)
            {
                in_namespace = true;
                indentation += get_indentement(line);
                continue;
            }
            if (!in_namespace)
                continue;

            indentation += get_indentement(line);
            if (indentation <= 0)
            {
                in_namespace = false;
                continue;
            }

            if (line.find("bool test_") != std::string::npos)
            {
                // extract just the function name
                size_t start = line.find("bool test_") + 5; // skip "bool "
                size_t end = line.find("(");
                if (end != std::string::npos)
                {
                    mod.functions.push_back(line.substr(start, end - start));
                }
            }
        }
    }
}

void find_test_functions(Module &mod)
{
    fs::path tests_dir = mod.path / "tests";
    find_test_functions_in_dir(mod, tests_dir);
}

// Writes the dispatch main. Freestanding: defines ENTRY_SYMBOL (not `int main`) and reports
// through the mlw_test:: interface (not printf), so it links under the freestanding build.
// The mlw_test:: functions are hand-written once against core's io; see REPORT_HEADER.
void write_main(const std::list<Module> &mods, const fs::path &outdir)
{
    fs::create_directories(outdir);
    std::ofstream file(outdir / "main.cpp");

    file << "// AUTO GENERATED - DO NOT EDIT\n\n";
    file << "#include \"" << REPORT_HEADER << "\"  // printing\n\n";

    // forward-declare every discovered test function in its namespace
    for (const Module &mod : mods)
    {
        if (mod.functions.empty())
            continue;
        file << "namespace " << mod.ns << " {\n";
        for (const std::string &fn : mod.functions)
            file << "    extern bool " << fn << "();\n";
        file << "}\n\n";
    }

    // entry point: entry's mlwStart calls this; its return goes to mlwExit
    file << "int32 " << ENTRY_SYMBOL << "()\n{\n";
    file << "    int passed = 0;\n";
    file << "    int failed = 0;\n\n";

    for (const Module &mod : mods)
    {
        if (mod.functions.empty())
            continue;
        file << "    println(\"" << mod.name << "\");\n";
        for (const std::string &fn : mod.functions)
        {
            file << "    if (" << mod.ns << "::" << fn << "()) { ++passed; println(\"  PASS  " << fn << "\"); }\n";
            file << "    else { ++failed; println(\" FAIL  " << fn << "\"); }\n";
        }
        file << "\n";
    }

    file << "    println(\"{} passed, {} failed\", passed, failed);\n";
    file << "    return failed > 0 ? 1 : 0;\n";
    file << "}\n";
}

int main()
{
    fs::path module_start = fs::path(ROOT_DIR) / "modules";

    std::list<Module> mods;
    find_modules(module_start, mods);
    filter_test_modules(mods);
    for (Module &m : mods)
        path_to_name(m);
    for (Module &m : mods)
        find_test_functions(m);

    write_main(mods, fs::path(ROOT_DIR) / "generated" / "tests");

    return 0;
}