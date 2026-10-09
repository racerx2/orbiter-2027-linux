# not upstream: collision addon, the vendored NVIDIA Blast subset (Extern/Blast) as the static PIC library CollBlastLib (Design CA-blast 1)
set(COLLISION_BLAST_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../Extern/Blast" CACHE PATH "The vendored NVIDIA Blast subset (Extern/Blast of the Orbiter source tree)")
get_filename_component(COLL_BLAST_DIR ${COLLISION_BLAST_DIR} ABSOLUTE)
if (NOT EXISTS ${COLL_BLAST_DIR}/include/lowlevel/NvBlast.h)
	message(FATAL_ERROR "Collision: no NVIDIA Blast subset at ${COLL_BLAST_DIR}; set COLLISION_BLAST_DIR to Extern/Blast of the Orbiter source tree")
endif ()
set(COLL_BLAST_INCLUDE include include/shared/NvFoundation include/lowlevel include/globals include/extensions/shaders include/extensions/stress)
list(TRANSFORM COLL_BLAST_INCLUDE PREPEND ${COLL_BLAST_DIR}/)

if (NOT TARGET CollBlastLib)
	set(simd source/shared/stress_solver/stress.cpp source/sdk/extensions/stress/NvBlastExtStressSolver.cpp) # the solver picks SIMD or scalar at run time
	set(srcs ${simd}
		source/sdk/common/NvBlastAssert.cpp source/sdk/common/NvBlastAtomic.cpp source/sdk/common/NvBlastTime.cpp source/sdk/common/NvBlastTimers.cpp
		source/sdk/globals/NvBlastGlobals.cpp source/sdk/globals/NvBlastInternalProfiler.cpp
		source/sdk/lowlevel/NvBlastActor.cpp source/sdk/lowlevel/NvBlastActorSerializationBlock.cpp source/sdk/lowlevel/NvBlastAsset.cpp
		source/sdk/lowlevel/NvBlastAssetHelper.cpp source/sdk/lowlevel/NvBlastFamily.cpp source/sdk/lowlevel/NvBlastFamilyGraph.cpp
		source/sdk/extensions/shaders/NvBlastExtDamageAccelerators.cpp source/sdk/extensions/shaders/NvBlastExtDamageAcceleratorAABBTree.cpp
		source/sdk/extensions/shaders/NvBlastExtDamageShaders.cpp)
	list(TRANSFORM simd PREPEND ${COLL_BLAST_DIR}/)
	list(TRANSFORM srcs PREPEND ${COLL_BLAST_DIR}/)
	add_library(CollBlastLib STATIC ${srcs})
	target_include_directories(CollBlastLib SYSTEM PUBLIC ${COLL_BLAST_INCLUDE})
	target_compile_definitions(CollBlastLib PUBLIC $<$<NOT:$<CONFIG:Release,RelWithDebInfo,MinSizeRel>>:_DEBUG>) # NvPreprocessor.h wants exactly one of NDEBUG and _DEBUG
	target_include_directories(CollBlastLib PRIVATE ${COLL_BLAST_DIR}/source/sdk/common ${COLL_BLAST_DIR}/source/sdk/lowlevel ${COLL_BLAST_DIR}/source/sdk/globals
		${COLL_BLAST_DIR}/source/sdk/extensions/shaders ${COLL_BLAST_DIR}/source/sdk/extensions/stress ${COLL_BLAST_DIR}/source/shared/NsFoundation/include ${COLL_BLAST_DIR}/source/shared/stress_solver)
	set_target_properties(CollBlastLib PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
		CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON FOLDER Extern)
	if (MSVC)
		target_compile_options(CollBlastLib PRIVATE /w) # no /arch: MSVC compiles the AVX/FMA intrinsics without it
	else ()
		target_compile_options(CollBlastLib PRIVATE -w "SHELL:-include limits" "SHELL:-include cstdio" "SHELL:-include cstdint" "SHELL:-include cstring") # upstream relies on MSVC's transitive includes
		if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
			set_source_files_properties(${simd} PROPERTIES COMPILE_OPTIONS "-mavx;-mfma") # stress.cpp checks AVX and FMA3 at run time, not AVX2
		endif ()
	endif ()
endif ()

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/CollBlastA.cpp)
	list(APPEND COLL_UNITS CollBlastA)
endif ()
