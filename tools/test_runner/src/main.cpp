#include <string>
#include <vector>
#include <list>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

struct Module{
    std::string name;
    std::string ns;
    fs::path path;
    std::vector<std::string> functions;
};


static const char* const ROOT_DIR = ".";

void find_modules(const fs::path& path, std::list<Module>& mods){
    for (const fs::directory_entry& entry : fs::directory_iterator(path)){
        if (!entry.is_directory()) continue;

        if(fs::exists(entry.path() / "CMakeLists.txt")){
            Module m;
            m.path = entry.path();
            mods.push_back(std::move(m));
        }else{
            find_modules(entry.path(), mods);
        }
    }
}

void filter_test_modules(std::list<Module>& mods) {
    auto it = mods.begin();
    while (it != mods.end()) {
        if (!fs::exists(it->path / "tests" / "CMakeLists.txt")) {
            it = mods.erase(it);
        } else {
            ++it;
        }
    }
}

void path_to_name(Module& mod){
    mod.name = std::string{};
    mod.ns = std::string{};

     bool found_modules = false;
    for (auto& part : mod.path) {
        if (!found_modules) {
            if (part.string() == "modules") found_modules = true;
            continue;
        }
        mod.name = part.string();
        mod.ns += part.string();
        mod.ns += '_';
    }
    mod.ns += "test";
}

//returns difrense in {}
int get_indentement(const std::string& line){
    int x =0;
    for(const char& c: line){
        if (c == '{') ++x;
        else if(c == '}') --x;
    }
    return x;
}
void find_test_functions_in_dir(Module& mod, const fs::path& path){
    for(auto& entry : fs::directory_iterator(path)){
        if (entry.is_directory()) {
            find_test_functions_in_dir(mod, entry.path()); // recurse
            continue;
        }
        if(entry.path().extension() != ".cpp") continue;

        std::ifstream file(entry.path());
        std::string line;
        bool in_namespace = false;
        int indentation = 0;

        while (std::getline(file, line)){
            if(line.find("namespace") != std::string::npos && line.find(mod.ns) != std::string::npos){
                in_namespace = true;
                indentation += get_indentement(line);
                continue;
            }
            if(!in_namespace) continue;
            
            indentation += get_indentement(line);
            if(indentation <= 0){
                in_namespace = false;
                continue;
            }

             if (line.find("bool test_") != std::string::npos) {
                // extract just the function name
                size_t start = line.find("bool test_") + 5; // skip "bool "
                size_t end   = line.find("(");
                if (end != std::string::npos) {
                    mod.functions.push_back(line.substr(start, end - start));
                }
            }
        }

    }
}

void find_test_functions(Module& mod){
    fs::path tests_dir = mod.path/"tests";
    find_test_functions_in_dir(mod, tests_dir);
}

void write_main(const std::list<Module>& mods, const fs::path& outdir){
    fs::create_directories(outdir);
    std::ofstream file(outdir / "main.cpp");

    file << "// AUTO GENERATED - DO NOT EDIT\n\n";
    file << "#include <cstdio>\n\n";
    

    for (const Module& mod : mods){
        if (mod.functions.empty()) continue;
        file << "namespace " << mod.ns << "{\n";
        for (const std::string& fn : mod.functions){
            file << "\textern bool " << fn << "();\n";
        } 
        file << "}\n\n";
    }

        // simple pass/fail tracking
    file << "int main() {\n";
    file << "    int passed = 0;\n";
    file << "    int failed = 0;\n\n";

    for (auto& mod : mods) {
         if (mod.functions.empty()) continue;
        file << "    printf(\"=== " << mod.name << " ===\\n\");\n";
        for (auto& fn : mod.functions) {
            file << "    if (" << mod.ns << "::" << fn << "()) {\n";
            file << "        passed++;\n";
            file << "        printf(\"  PASS  " << fn << "\\n\");\n";
            file << "    } else {\n";
            file << "        failed++;\n";
            file << "        printf(\"  FAIL  " << fn << "\\n\");\n";
            file << "    }\n";
        }
        file << "    printf(\"\\n\");\n\n";

    }

    file << "    printf(\"\\n%d passed, %d failed\\n\", passed, failed);\n";
    file << "    return failed > 0 ? 1 : 0;\n";
    file << "}\n";
}



void write_cmake(const std::list<Module>& mods, const fs::path& out_dir, const fs::path& repo_root) {
    std::ofstream file(out_dir / "CMakeLists.txt");

    file << "# AUTO GENERATED - DO NOT EDIT\n\n";
    file << "cmake_minimum_required(VERSION 3.21)\n";
    file << "project(all_tests LANGUAGES CXX)\n\n";
    file << "set(CMAKE_CXX_STANDARD 20)\n";
    file << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    file << "include(${CMAKE_CURRENT_SOURCE_DIR}/../../tools/cmake/definitions.cmake)\n\n";

    // add each module
    for (auto& mod : mods) {
        file << "add_subdirectory(" << (fs::absolute( repo_root / mod.path / "tests")).generic_string() << " " << mod.name << ")\n";
    }
    file << "\n";

    // collect all test cpp files
    file << "add_executable(all_tests\n";
    file << "    main.cpp\n";
    file << ")\n\n";

    // link against each module
    file << "target_link_libraries(all_tests\n";
    file << "    PRIVATE\n";
    for (auto& mod : mods) {
        file << "        " << mod.name << "_test" << "\n";
    }
    file << ")\n";
}

int main(){
    fs::path module_start = fs::path(ROOT_DIR) / "modules";

    std::list<Module> mods;
    find_modules(module_start, mods);
    filter_test_modules(mods);
    for (Module& m : mods){
        path_to_name(m);
    }
    for (Module& m : mods){
        find_test_functions(m);
    }

    write_main(mods, fs::path(ROOT_DIR) / "generated" / "tests");
    write_cmake(mods, fs::path(ROOT_DIR) / "generated" / "tests", fs::path(ROOT_DIR));

    return 0;
}