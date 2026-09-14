cmake_minimum_required(VERSION 3.24)
project(HExplorer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_INSTALL_BINDIR "." CACHE STRING "Runtime installation directory")
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Quick QuickControls2 Concurrent Test)
qt_standard_project_setup(REQUIRES 6.8)
if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
endif()

include(CTest)
option(HEXPLORER_CORE_ONLY "Build only the filesystem and workspace model tests" OFF)
qt_add_library(hexplorer_core STATIC
    src/core/DirectoryModel.cpp src/core/DirectoryModel.h
    src/core/PaneController.cpp src/core/PaneController.h
    src/core/Workspace.cpp src/core/Workspace.h
)
target_include_directories(hexplorer_core PUBLIC src/core)
target_link_libraries(hexplorer_core PUBLIC Qt6::Core Qt6::Concurrent)
if(BUILD_TESTING)
    qt_add_executable(coretests tests/coretests.cpp)
    target_link_libraries(coretests PRIVATE hexplorer_core Qt6::Test)
    add_test(NAME core COMMAND coretests -o -,txt)
endif()
if(HEXPLORER_CORE_ONLY)
    return()
endif()

add_subdirectory(src/operations)
add_subdirectory(src/platform)
qt_add_library(hexplorer_app STATIC src/core/AppController.cpp src/core/AppController.h)
target_link_libraries(hexplorer_app PUBLIC hexplorer_core Qt6::Widgets Qt6::Quick
    hexplorer_operations hexplorer_platform)

qt_add_executable(HExplorer WIN32 src/main.cpp)
qt_add_resources(HExplorer app_assets PREFIX "/" FILES assets/hexplorer.svg)
qt_add_qml_module(HExplorer
    URI HExplorer
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/FilePane.qml
        qml/Sidebar.qml
        qml/TaskCenter.qml
)
target_link_libraries(HExplorer PRIVATE hexplorer_app Qt6::QuickControls2)
if(WIN32)
    target_sources(HExplorer PRIVATE packaging/app.rc)
endif()

if(BUILD_TESTING)
    qt_add_executable(apptests tests/apptests.cpp)
    target_link_libraries(apptests PRIVATE hexplorer_app Qt6::Test)
    add_test(NAME app COMMAND apptests -o -,txt)
    set_tests_properties(app PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/operationtests.cpp")
        qt_add_executable(operationtests tests/operationtests.cpp)
        target_link_libraries(operationtests PRIVATE hexplorer_operations Qt6::Test)
        add_test(NAME operations COMMAND operationtests -o -,txt)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/platformtests.cpp")
        qt_add_executable(platformtests tests/platformtests.cpp)
        target_link_libraries(platformtests PRIVATE hexplorer_platform Qt6::Test)
        add_test(NAME platform COMMAND platformtests -o -,txt)
    endif()
    add_test(NAME qml_smoke COMMAND HExplorer --smoke-test --smoke-log "${CMAKE_CURRENT_BINARY_DIR}/qml-smoke.log")
    set_tests_properties(qml_smoke PROPERTIES TIMEOUT 30)
endif()

install(TARGETS HExplorer RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" BUNDLE DESTINATION .)
if(MSVC AND DEFINED ENV{VCToolsRedistDir})
    file(TO_CMAKE_PATH "$ENV{VCToolsRedistDir}" msvc_redist_root)
    file(GLOB msvc_runtime_dlls "${msvc_redist_root}/x64/Microsoft.VC*.CRT/*.dll")
    install(FILES ${msvc_runtime_dlls} DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
install(FILES README.md packaging/NOTICE.txt DESTINATION .)
FetchContent_GetProperties(miniz SOURCE_DIR miniz_license_source)
install(FILES "${miniz_license_source}/LICENSE" DESTINATION licenses RENAME miniz-LICENSE.txt)
qt_generate_deploy_qml_app_script(
    TARGET HExplorer OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
set(CPACK_PACKAGE_NAME HExplorer)
set(CPACK_PACKAGE_VENDOR HExplorer)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR ZIP)
include(CPack)
