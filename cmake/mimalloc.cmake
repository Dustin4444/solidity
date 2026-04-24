include(FetchContent)

FetchContent_Declare(
	mimalloc
	GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
	GIT_TAG v3.3.1
	GIT_SHALLOW TRUE
)

set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(MI_OVERRIDE ON CACHE BOOL "" FORCE)

FetchContent_GetProperties(mimalloc)
if (NOT mimalloc_POPULATED)
	FetchContent_Populate(mimalloc)
	add_subdirectory(${mimalloc_SOURCE_DIR} ${mimalloc_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
