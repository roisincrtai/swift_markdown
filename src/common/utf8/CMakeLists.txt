add_library(swift_markdown_utf8 STATIC utf8.cpp)
target_include_directories(swift_markdown_utf8 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(swift_markdown_utf8 PUBLIC cxx_std_17)
