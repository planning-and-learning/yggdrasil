foreach(wrapper IN ITEMS mpicc.sh.in mpicc.bash.in mpicxx.sh.in mpicxx.bash.in)
    set(path "${MPICH_SOURCE_DIR}/src/env/${wrapper}")
    file(READ "${path}" contents)
    string(REPLACE
        "prefix=__PREFIX_TO_BE_FILLED_AT_INSTALL_TIME__"
        [=[prefix=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE:-$0}")/.." && pwd)]=]
        contents "${contents}"
    )
    string(REPLACE
        "exec_prefix=__EXEC_PREFIX_TO_BE_FILLED_AT_INSTALL_TIME__"
        [=[exec_prefix=${prefix}]=]
        contents "${contents}"
    )
    string(REPLACE
        "sysconfdir=__SYSCONFDIR_TO_BE_FILLED_AT_INSTALL_TIME__"
        [=[sysconfdir=${prefix}/etc]=]
        contents "${contents}"
    )
    string(REPLACE
        "includedir=__INCLUDEDIR_TO_BE_FILLED_AT_INSTALL_TIME__"
        [=[includedir=${prefix}/include]=]
        contents "${contents}"
    )
    string(REPLACE
        "libdir=__LIBDIR_TO_BE_FILLED_AT_INSTALL_TIME__"
        "libdir=\${prefix}/${MPICH_INSTALL_LIBDIR}"
        contents "${contents}"
    )
    if(wrapper MATCHES "^mpicc")
        string(REPLACE "CC=\"@CC@\"" "CC=\"cc\"" contents "${contents}")
    else()
        string(REPLACE "CXX=\"@CXX@\"" "CXX=\"c++\"" contents "${contents}")
    endif()
    file(WRITE "${path}" "${contents}")
endforeach()
