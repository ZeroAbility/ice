# CompileSlice - script slice compile with cmake similar to IceBuilder
# 
# Params:
# slice2bin_params - parameters passed for specific slice2_bin use cases
# slice_include_paths - include dir(s) needed for -I parameter
# ice_file - file to be compiled (<filename>.ice)
# output_dir - generated source output path
# output_file - generated source output file name

function(CompileSlice slice2bin_params slice_include_paths ice_file output_dir output_file)
  set(depfile "${output_dir}/${ice_name}.d"
  if(EXISTS "${depfile}")
    file(STRINGS "${depfile}" ice_children)
    list(POP_FRONT ice_children)
    foreach(child IN LISTS ice_children)
      string(REPLACE "\"" "" child "${child}")
    endforeach()
  endif()

  get_filename_component(ice_name "${ice_file}" NAME_WE)
  add_custom_command(OUTPUT "${output_file}"
    COMMAND ${SLICE2_BIN} ${slice_include_paths} 
                          --depend
                          --depend-file "${depfile}"
                          "${ice_file}"
    COMMAND ${SLICE2_BIN} ${slice_include_paths} 
                          ${slice2bin_params} 
                          "${ice_file}"
    COMMENT "Generating sources for ${ice_file}..."
    DEPENDS "${ice_file}"
    BYPRODUCTS "${depfile}"
    WORKING_DIRECTORY "${output_dir}"
    VERBATIM)
endfunction()
