include_guard(GLOBAL)
include(FetchContent)

# Scope upstream's generic option names so they do not configure the application.
function(hexplorer_add_miniz)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_FUZZERS OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(INSTALL_PROJECT OFF)
    set(AMALGAMATE_SOURCES OFF)
    set(BUILD_HEADER_ONLY OFF)
    FetchContent_Declare(miniz
        GIT_REPOSITORY https://github.com/richgel999/miniz.git
        GIT_TAG 293d4db1b7d0ffee9756d035b9ac6f7431ef8492 # release tag 3.0.2
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(miniz)
endfunction()
hexplorer_add_miniz()
