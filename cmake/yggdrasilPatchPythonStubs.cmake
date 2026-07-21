# Install-time stub post-processing shared by the provider wheels
# (pyyggdrasil, pypddl, pytyr, pyrunir). Shipped with the yggdrasil CMake
# package; consumers include it from install(CODE ...) and call:
#
#   yggdrasil_patch_python_stubs(
#       PACKAGE <pkg>                 # e.g. pypddl; operates on <prefix>/<pkg>
#       [PRIVATE_MODULE <_pkg>]       # migrate <pkg>/<_pkg>[.<ABI>]/*.pyi to
#                                     # public locations first (existing/
#                                     # handwritten public stubs win), then
#                                     # remove the private stub directories
#       RENAME_PACKAGES <p1> <p2> ... # rewrite <p>._<p>. -> <p>. and
#                                     # <p>._<p> -> <p> in all *.pyi files
#   )
#
# The private extension module of package <p> is `_<p>` by repo-family
# convention. Honors $ENV{DESTDIR} like every other install rule.

function(yggdrasil_patch_python_stubs)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "PACKAGE;PRIVATE_MODULE" "RENAME_PACKAGES")
    if(NOT ARG_PACKAGE)
        message(FATAL_ERROR "yggdrasil_patch_python_stubs: PACKAGE is required")
    endif()

    set(stub_root "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${ARG_PACKAGE}")
    if(NOT EXISTS "${stub_root}")
        return()
    endif()

    if(ARG_PRIVATE_MODULE)
        file(GLOB private_stub_roots LIST_DIRECTORIES true
             "${stub_root}/${ARG_PRIVATE_MODULE}"
             "${stub_root}/${ARG_PRIVATE_MODULE}.*")
        list(SORT private_stub_roots)

        foreach(private_stub_root IN LISTS private_stub_roots)
            if(NOT IS_DIRECTORY "${private_stub_root}")
                continue()
            endif()

            file(GLOB_RECURSE private_stub_files "${private_stub_root}/*.pyi")
            list(SORT private_stub_files)

            foreach(private_stub_file IN LISTS private_stub_files)
                file(RELATIVE_PATH relative_stub_path "${private_stub_root}" "${private_stub_file}")
                set(public_stub_file "${stub_root}/${relative_stub_path}")

                # <name>.pyi belonging to an existing package directory becomes
                # <name>/__init__.pyi.
                string(REGEX REPLACE "\\.pyi$" "" public_package_dir "${public_stub_file}")
                if(IS_DIRECTORY "${public_package_dir}")
                    set(public_stub_file "${public_package_dir}/__init__.pyi")
                endif()

                # Handwritten public stubs win over generated ones.
                if(EXISTS "${public_stub_file}")
                    continue()
                endif()

                get_filename_component(public_stub_dir "${public_stub_file}" DIRECTORY)
                file(MAKE_DIRECTORY "${public_stub_dir}")
                file(READ "${private_stub_file}" private_stub_content)
                file(WRITE "${public_stub_file}" "${private_stub_content}")
            endforeach()

            file(REMOVE_RECURSE "${private_stub_root}")
        endforeach()
    endif()

    file(GLOB_RECURSE stub_files "${stub_root}/*.pyi")
    list(SORT stub_files)

    foreach(stub_file IN LISTS stub_files)
        file(READ "${stub_file}" stub_content)
        set(patched_stub_content "${stub_content}")

        foreach(rename_package IN LISTS ARG_RENAME_PACKAGES)
            string(REPLACE "${rename_package}._${rename_package}." "${rename_package}."
                   patched_stub_content "${patched_stub_content}")
            string(REPLACE "${rename_package}._${rename_package}" "${rename_package}"
                   patched_stub_content "${patched_stub_content}")
        endforeach()

        # nanobind stubgen emits bare `os.PathLike` for filesystem::path params; subscript it
        # so strict type checkers do not see PathLike[Unknown]. Generated stubs never contain
        # the subscripted form, so a plain replace is idempotent per install.
        string(REGEX REPLACE "os\\.PathLike([^[])" "os.PathLike[str]\\1"
               patched_stub_content "${patched_stub_content}")

        if(NOT stub_content STREQUAL patched_stub_content)
            file(WRITE "${stub_file}" "${patched_stub_content}")
        endif()
    endforeach()
endfunction()
