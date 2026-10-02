# How to add new dependency

If you want to add new dependency, the way to add it will depend on the type

## Lua module dependency

The Lua modules are source copies in `Extern/ldoc`, `Extern/Penlight` and `Extern/luafilesystem`.
The `CopyLDoc` target in Extern/CMakeLists.txt copies ldoc and Penlight into the build, and
luafilesystem is built there as the `lfs` library. Add a new module the same way: a copy of its
sources, then a copy step (pure Lua) or a library target (C) in Extern/CMakeLists.txt.

## C++ dependency

1. Create a new directory (e.g. `mylib`)
1. Add a line to Extern/CMakeLists.txt: `add_subdirectory(mylib)`
1. Copy `Extern/Lua/CMakeLists.txt` to new directory
1. Edit the file replacing `lua` with `mylib` and adding new repository URL
1. Add other tweaks to `CMakeLists.txt` as necessary
