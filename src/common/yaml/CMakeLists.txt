add_library(swift_markdown_yaml STATIC yaml.cpp yaml_parser.cpp yaml_scanner.cpp)
target_include_directories(swift_markdown_yaml PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(swift_markdown_yaml PUBLIC swift_markdown_json)
target_compile_features(swift_markdown_yaml PUBLIC cxx_std_17)
