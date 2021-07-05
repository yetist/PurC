# Append the all C files in the specified directory list to the source list
macro(APPEND_ALL_SOURCE_FILES_IN_DIRLIST result)
    set(filelist "")
    foreach(module ${ARGN})
        list(APPEND filelist ${module}/*.c)
        list(APPEND filelist ${module}/*.cpp)
    endforeach()
    file(GLOB_RECURSE ${result} RELATIVE ${PURC_DIR} ${filelist})
#    FOREACH(file ${${result}})
#        message(STATUS ${file})
#    ENDFOREACH()
    unset(filelist)
endmacro()
