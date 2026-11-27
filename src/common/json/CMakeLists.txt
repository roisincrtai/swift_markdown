add_library(swift_markdown_json STATIC json.cpp)
target_include_directories(swift_markdown_json PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(swift_markdown_json PRIVATE swift_markdown_utf8)
target_compile_features(swift_markdown_json PUBLIC cxx_std_17)
