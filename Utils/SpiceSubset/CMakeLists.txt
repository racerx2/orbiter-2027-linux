# not upstream: copies SPK segments into a new kernel (Kernels/kerberos_styx.bsp from JPL's plu060.bsp)

add_executable(spicesubset
	spicesubset.c
)

target_include_directories(spicesubset
	PRIVATE ${CMAKE_SOURCE_DIR}/Src/Celbody/SPICE/cspice/linux_x64/include
)

target_link_libraries(spicesubset
	${CMAKE_SOURCE_DIR}/Src/Celbody/SPICE/cspice/linux_x64/lib/cspice.a
	m
)

set_target_properties(spicesubset
	PROPERTIES
	FOLDER Tools
)
