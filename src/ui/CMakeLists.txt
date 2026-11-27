set(app_sources Application.swift MarkdownDocument.swift Viewer.swift Preview.swift SourceEditor.swift)
set(preview "${CMAKE_CURRENT_SOURCE_DIR}/Preview.html")
set_source_files_properties("${preview}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

set(SWIFT_MARKDOWN_ICON "${PROJECT_SOURCE_DIR}/src/logo/logo.svg" CACHE FILEPATH "Selected vector app icon (SVG)")
if(NOT EXISTS "${SWIFT_MARKDOWN_ICON}" OR NOT SWIFT_MARKDOWN_ICON MATCHES "\\.[sS][vV][gG]$")
    message(FATAL_ERROR "SWIFT_MARKDOWN_ICON must point to an existing SVG.")
endif()

set(icon_source_catalog "${PROJECT_SOURCE_DIR}/src/logo/Assets.xcassets")
set(icon_catalog "${CMAKE_CURRENT_BINARY_DIR}/AppAssets.xcassets")
set(icon_set "${icon_catalog}/AppIcon.appiconset")
set(icon_svg "${icon_set}/logo.svg")
configure_file("${icon_source_catalog}/Contents.json" "${icon_catalog}/Contents.json" COPYONLY)
configure_file("${icon_source_catalog}/AppIcon.appiconset/Contents.json" "${icon_set}/Contents.json" COPYONLY)
configure_file("${SWIFT_MARKDOWN_ICON}" "${icon_svg}" COPYONLY)

set(tools_workdir "${PROJECT_SOURCE_DIR}/build/artefacts")
file(MAKE_DIRECTORY "${tools_workdir}")
set(icon_output "${CMAKE_CURRENT_BINARY_DIR}/icon-output")
set(compiled_icon "${icon_output}/Assets.car")
add_custom_command(
    OUTPUT "${compiled_icon}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${icon_output}"
    COMMAND /usr/bin/xcrun actool
            --compile "${icon_output}" --platform macosx
            --minimum-deployment-target "${CMAKE_OSX_DEPLOYMENT_TARGET}"
            --app-icon AppIcon --output-partial-info-plist "${icon_output}/AppIcon.plist"
            "${icon_catalog}"
    DEPENDS "${icon_svg}" "${icon_catalog}/Contents.json" "${icon_set}/Contents.json"
    WORKING_DIRECTORY "${tools_workdir}"
    COMMENT "Compiling the vector app icon"
    VERBATIM
)
set_source_files_properties("${icon_svg}" "${compiled_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

configure_file(Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
add_executable(SwiftMarkdown MACOSX_BUNDLE ${app_sources} "${preview}" "${icon_svg}" "${compiled_icon}"
    ../bridge/markdown_bridge.cpp
)
target_link_libraries(SwiftMarkdown PRIVATE swift_markdown_core
    "-framework AppKit" "-framework Foundation" "-framework SwiftUI" "-framework WebKit"
    "-framework UniformTypeIdentifiers"
)
target_include_directories(SwiftMarkdown PRIVATE ../bridge)
set_target_properties(SwiftMarkdown PROPERTIES
    Swift_MODULE_NAME SwiftMarkdown
    OUTPUT_NAME "Swift Markdown"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.swiftmarkdown.viewer"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
    XCODE_ATTRIBUTE_SWIFT_INCLUDE_PATHS "$(inherited) \"${PROJECT_SOURCE_DIR}/src/bridge\""
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/build/unsigned"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PROJECT_SOURCE_DIR}/build/unsigned"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PROJECT_SOURCE_DIR}/build/unsigned/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${PROJECT_SOURCE_DIR}/build/unsigned/RelWithDebInfo"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${PROJECT_SOURCE_DIR}/build/unsigned/MinSizeRel"
)

if(CMAKE_GENERATOR MATCHES "Ninja")
    if(CMAKE_OSX_ARCHITECTURES)
        list(LENGTH CMAKE_OSX_ARCHITECTURES architecture_count)
        if(architecture_count GREATER 1)
            message(FATAL_ERROR "Use the Xcode preset to build multiple macOS architectures.")
        endif()
        set(swift_architecture "${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(swift_architecture "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    set(swift_target "${swift_architecture}-apple-macosx${CMAKE_OSX_DEPLOYMENT_TARGET}")
    target_compile_options(SwiftMarkdown PRIVATE "$<$<COMPILE_LANGUAGE:Swift>:SHELL:-target ${swift_target}>")
    target_link_options(SwiftMarkdown PRIVATE "SHELL:-target ${swift_target}")
    add_custom_command(TARGET SwiftMarkdown POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -f "$<TARGET_FILE_DIR:SwiftMarkdown>/main.d"
        VERBATIM
    )
endif()

set(CODE_SIGN_IDENTITY "" CACHE STRING "Signing identity for deploy. Empty auto-detects. Set - for ad hoc.")
add_custom_target(deploy
    COMMAND "${PROJECT_SOURCE_DIR}/src/scripts/macos/deploy.sh"
            "$<TARGET_BUNDLE_DIR:SwiftMarkdown>" "${PROJECT_SOURCE_DIR}/build/dist"
            "${PROJECT_VERSION}" "${CODE_SIGN_IDENTITY}"
    DEPENDS SwiftMarkdown
    USES_TERMINAL
    VERBATIM
)

add_custom_target(sign
    COMMAND "${PROJECT_SOURCE_DIR}/src/scripts/macos/deploy.sh"
            "$<TARGET_BUNDLE_DIR:SwiftMarkdown>" "${PROJECT_SOURCE_DIR}/build/dist"
            "${PROJECT_VERSION}" "${CODE_SIGN_IDENTITY}" --app-only
    DEPENDS SwiftMarkdown
    USES_TERMINAL
    VERBATIM
)
