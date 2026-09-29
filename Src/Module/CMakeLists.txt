# Copyright (c) Martin Schweiger
# Licensed under the MIT License

# not upstream: module dialogs and drawing are Qt (the SDK's HWND/HDC are QWidget/QPainter)
set(CMAKE_AUTOMOC ON)
link_libraries(Qt6::Widgets)

add_subdirectory(LuaScript)
