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

# GCC 16 promoted -Wincompatible-pointer-types to an error.
# lib3mf's bundled libzip assigns LPCSTR/LPCWSTR Win32 function pointers into
# void* struct fields in these two Windows-only sources — inject a pragma to
# suppress the error before it fires (target_compile_options cannot reach these
# files because they are compiled as part of an internal FetchContent target).
set(pragma_guard
"#ifdef __GNUC__
#pragma GCC diagnostic ignored \"-Wincompatible-pointer-types\"
#endif
")
set(win_files_to_patch
    "Libraries/libzip/Source/win/zip_source_file_win32_ansi.c"
    "Libraries/libzip/Source/win/zip_source_file_win32_utf16.c"
)

foreach(f ${win_files_to_patch})
    if(EXISTS "${f}")
        file(READ "${f}" content)
        if(NOT content MATCHES "Wincompatible-pointer-types")
            file(WRITE "${f}" "${pragma_guard}${content}")
            message(STATUS "lib3mf patch: suppressed -Wincompatible-pointer-types in ${f}")
        endif()
    endif()
endforeach()
