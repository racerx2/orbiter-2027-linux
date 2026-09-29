function(enable_sanitizer SANITIZER)
  add_compile_options(-fsanitize=${SANITIZER} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${SANITIZER})
  # MSVC branches (CRT debug heap, clang_rt.asan DLL and llvm-symbolizer.exe copies) left out: GCC links libasan itself
  message(STATUS "Enabled sanitize=${SANITIZER}")
endfunction()
