add_library(swift_markdown_tex STATIC
    tex.cpp lexer.cpp macros.cpp parser.cpp layout.cpp environments.cpp symbols.cpp mathml.cpp
)
target_include_directories(swift_markdown_tex PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(swift_markdown_tex PUBLIC cxx_std_20)

if(BUILD_TESTING)
    add_executable(tex_tests tests/tex_tests.cpp)
    target_link_libraries(tex_tests PRIVATE swift_markdown_tex)
    add_test(NAME tex.formulas COMMAND tex_tests)
endif()
