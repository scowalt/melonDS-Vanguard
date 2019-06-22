# 3 june 2016

include_directories("C:/Users/Daniel/Documents/deps")
include_directories("C:/Users/Daniel/Documents/deps/SDL_dev")
link_directories("C:/Users/Daniel/Documents/deps/SDL_dev/libs")
list(APPEND _LIBUI_SOURCES
	common/areaevents.c
	common/control.c
	common/debug.c
	common/matrix.c
	common/shouldquit.c
	common/userbugs.c
)
set(_LIBUI_SOURCES ${_LIBUI_SOURCES} PARENT_SCOPE)

list(APPEND _LIBUI_INCLUDEDIRS
	common
)
set(_LIBUI_INCLUDEDIRS ${_LIBUI_INCLUDEDIRS} PARENT_SCOPE)
