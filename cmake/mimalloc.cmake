include(FetchContent)

FetchContent_Declare(
	mimalloc
	GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
	GIT_TAG v2.2.2
	GIT_SHALLOW TRUE
)

set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(MI_OVERRIDE ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(mimalloc)
