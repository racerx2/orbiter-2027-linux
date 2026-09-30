# Reset the planet texture directory to the default local Textures directory
# before packing

# get the source directory
list(GET CPACK_BUILD_SOURCE_DIRS 0 src_dir)

# CMAKE_INSTALL_PREFIX note left out: the script now writes the staged tree
set(inst_dir ${CPACK_TEMPORARY_INSTALL_DIRECTORY}/Orbiter) # not upstream: the staged tree, not the install prefix
set(ORBITER_PLANET_TEXTURE_INSTALL_DIR_W "Textures") # not upstream: no '.\' prefix, Linux paths keep '/'
if(EXISTS ${inst_dir}/Orbiter.cfg) # not upstream: only a custom texture dir installs Orbiter.cfg
	configure_file(${src_dir}/Src/Orbiter/Orbiter.cfg.in ${inst_dir}/Orbiter.cfg)
endif()
# Orbiter_NG.cfg left out: Windows only
