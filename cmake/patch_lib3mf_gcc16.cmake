# Applied via FetchContent PATCH_COMMAND after lib3mf is checked out.
# GCC 16 no longer provides <algorithm> transitively, so std::find / std::sort
# etc. fail to compile in these lib3mf source files.
set(files_to_patch
    "Source/API/lib3mf_function.cpp"
    "Source/API/lib3mf_contentencryptionparams.cpp"
    "Source/API/lib3mf_resourcedata.cpp"
    "Source/Model/Classes/NMR_ModelSliceStack.cpp"
    "Source/Model/Classes/NMR_Model.cpp"
    "Source/Model/Writer/v100/NMR_ResourceDependencySorter.cpp"
)

foreach(f ${files_to_patch})
    if(EXISTS "${f}")
        file(READ "${f}" content)
        if(NOT content MATCHES "#include <algorithm>")
            file(WRITE "${f}" "#include <algorithm>\n${content}")
            message(STATUS "lib3mf patch: added <algorithm> to ${f}")
        endif()
    endif()
endforeach()
