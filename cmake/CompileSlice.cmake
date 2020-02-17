# CompileSlice - script slice compile with cmake similar to IceBuilder
# 
# Params:
# slice2bin_params - parameters passed for specific slice2_bin use cases
# slice_include_paths - include dir(s) needed for -I parameter
# ice_file - file to be compiled (<filename>.ice)
# output_dir - generated source output path
# output_file - generated source output file name

function(CompileSlice slice2bin_params slice_include_paths ice_file output_dir output_file)
	set(ice_children "")
	if(EXISTS ${output_dir}/${ice_name}.d)
		file(STRINGS ${output_dir}/${ice_name}.d ice_children)
		list(LENGTH ${ice_children} ice_children_len)
		foreach(child IN LISTS ${ice_children} RANGE 1 ${ice_children_len} 1)
			string(REPLACE "\"" "" child ${child})
		endforeach()
	endif()

	get_filename_component(ice_name ${ice_file} NAME_WE)
	add_custom_command(
		OUTPUT 
			${output_dir}/${ice_name}.d 
			${output_file}
		COMMAND ${SLICE2_BIN}
		ARGS 
			${slice_include_paths} 
			--depend 
			--depend-file ${ice_name}.d
			${ice_file}
		COMMAND ${SLICE2_BIN}
		ARGS 
			${slice_include_paths} 
			${slice2bin_params} 
			${ice_file}
		COMMENT "Generating sources for ${ice_file}..."
		BYPRODUCTS ${output_dir}/${ice_name}.d ${output_file}
		WORKING_DIRECTORY ${output_dir}
		MAIN_DEPENDENCY ${ice_file}
		DEPENDS ${SLICE2_BIN}
	)
endfunction()
