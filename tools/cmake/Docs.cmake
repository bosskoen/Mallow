# cmake/Docs.cmake
#
# Adds a `docs` target that runs Doxygen into ${CMAKE_BINARY_DIR}/docs.
# Nothing is written into the source tree, and no Doxyfile is committed:
# the config below is handed to Doxygen by CMake at generate time.
#
# Requires Doxygen. Graphs also require Graphviz (the `dot` program).
# The target is on-demand only: it does NOT build as part of a normal build.

find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(NOT DOXYGEN_FOUND)
    message(STATUS "Doxygen not found -- `docs` target disabled")
    return()
endif()

# --- output: build tree only, never the source tree ------------------------
set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/docs)

# --- extraction ------------------------------------------------------------
set(DOXYGEN_EXTRACT_ALL          NO)        # full skeleton + graphs, zero comments needed
set(DOXYGEN_RECURSIVE            YES)
set(DOXYGEN_EXCLUDE_SYMBOLS      "*::detail") # hide internal helper namespaces
set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)         # quiet while you document incrementally
set(DOXYGEN_WARN_NO_PARAMDOC YES)
set(DOXYGEN_WARN_LOGFILE         ${CMAKE_BINARY_DIR}/docs/undocumented.txt)


# --- output formats: HTML only. LaTeX is what spews dozens of files. --------
set(DOXYGEN_GENERATE_HTML        YES)
set(DOXYGEN_GENERATE_LATEX       NO)
set(DOXYGEN_GENERATE_TREEVIEW    YES)
set(DOXYGEN_QUIET                YES)

# --- dependency graphs (need the `dot` component from Graphviz) -------------
if(TARGET Doxygen::dot)
    set(DOXYGEN_HAVE_DOT            YES)
    set(DOXYGEN_DOT_IMAGE_FORMAT    svg)
    set(DOXYGEN_INTERACTIVE_SVG     YES)
    set(DOXYGEN_INCLUDE_GRAPH       YES)     # per-file #include graph
    set(DOXYGEN_INCLUDED_BY_GRAPH   YES)
    set(DOXYGEN_DIRECTORY_GRAPH     YES)     # folder-to-folder deps: your layering check
    set(DOXYGEN_COLLABORATION_GRAPH YES)
    set(DOXYGEN_CLASS_GRAPH         YES)
else()
    message(STATUS "Graphviz (dot) not found -- docs will build without graphs")
endif()

set(DOXYGEN_EXCLUDE_PATTERNS
    "*/build/*"
    "*/test/*"
    "*/tests/*"
    "*/private/*"
)

# The target. Point it at wherever your headers actually live.
# Change `include` below to `src`, or list several paths, to match your layout.
doxygen_add_docs(docs
   # ${PROJECT_SOURCE_DIR}/app
    ${PROJECT_SOURCE_DIR}/modules
    COMMENT "Generating API docs into ${DOXYGEN_OUTPUT_DIRECTORY}/html"
)