include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/Archive.cmake")
qt_add_library(hexplorer_operations STATIC
    TaskManager.h TaskManager.cpp
    FileOperationWorker.h FileOperationWorker.cpp
)
target_include_directories(hexplorer_operations PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(hexplorer_operations PUBLIC Qt6::Core Qt6::Concurrent miniz)
target_compile_features(hexplorer_operations PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(hexplorer_operations PRIVATE shell32 ole32)
endif()
