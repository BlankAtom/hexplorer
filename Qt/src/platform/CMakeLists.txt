qt_add_library(hexplorer_platform STATIC
    NativeServices.cpp NativeServices.h
    PreviewController.cpp PreviewController.h
    ShellIconProvider.cpp ShellIconProvider.h
)
target_include_directories(hexplorer_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(hexplorer_platform PUBLIC Qt6::Core Qt6::Gui Qt6::Quick Qt6::Concurrent)
if(WIN32)
    target_compile_definitions(hexplorer_platform PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(hexplorer_platform PRIVATE shell32 ole32 uuid user32 gdi32)
endif()
