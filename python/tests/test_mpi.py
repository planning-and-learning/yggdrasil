import os
import platform
import subprocess
import tempfile
import textwrap
from importlib import metadata as importlib_metadata
from pathlib import Path

import pyyggdrasil


def _check_distribution_compliance() -> None:
    prefix = pyyggdrasil.native_prefix()
    license_dir = prefix / "share" / "licenses"
    expected_license_files = {
        "argparse/LICENSE",
        "benchmark/LICENSE",
        "boost/LICENSE_1_0.txt",
        "cista/LICENSE",
        "fmt/LICENSE",
        "googletest/LICENSE",
        "gtl/LICENSE",
        "gtl/license_folly",
        "gtl/license_lamerman",
        "icu/LICENSE",
        "mpich/COPYRIGHT",
        "mpich/LICENSE.json-c",
        "mpich/LICENSE.libfabric",
        "mpich/THIRD_PARTY_NOTICES.libfabric",
        "nanobind/LICENSE",
        "nanobind/robin_map/LICENSE",
        "nauty/COPYRIGHT",
        "nauty/LICENSE-2.0.txt",
        "oneTBB/LICENSE.txt",
        "oneTBB/third-party-programs.txt",
        "tomlplusplus/LICENSE",
    }
    actual_license_files = {
        path.relative_to(license_dir).as_posix()
        for path in license_dir.rglob("*")
        if path.is_file()
    }
    assert actual_license_files == expected_license_files
    assert (prefix / "share" / "CORRESPONDING_SOURCE.md").is_file()
    assert (prefix / "share" / "SOURCES.json").is_file()
    assert "Modified by the Yggdrasil project on 2026-08-13" in (
        prefix / "include" / "gtl" / "btree.hpp"
    ).read_text(encoding="utf-8")

    distribution = importlib_metadata.distribution("pyyggdrasil")
    assert distribution.metadata["License-Expression"] == "GPL-3.0-or-later"
    assert distribution.metadata.get_all("License-File") == ["LICENSE"]
    assert any(
        str(path).endswith(".dist-info/licenses/LICENSE")
        for path in distribution.files or ()
    )


def _run_smoke_test(root: Path) -> None:
    _check_distribution_compliance()
    prefix = pyyggdrasil.native_prefix()
    mpicxx = prefix / "bin" / "mpicxx"
    mpiexec = prefix / "bin" / "mpiexec"
    for tool in (
        prefix / "bin" / "mpicc",
        mpicxx,
        mpiexec,
        prefix / "bin" / "hydra_pmi_proxy",
    ):
        assert tool.is_file()
        assert os.access(tool, os.X_OK)

    assert not list(prefix.glob("lib*/libtbbbind*"))
    if platform.system() == "Linux":
        repaired_libraries = prefix.parent / "pyyggdrasil.libs"
        assert not any(
            path.name.startswith(("libcap-", "libhwloc-", "libudev-"))
            for path in repaired_libraries.glob("*")
        )

    source_dir = root / "src"
    build_dir = root / "build"
    source_dir.mkdir()
    (source_dir / "CMakeLists.txt").write_text(
        textwrap.dedent(
            """\
            cmake_minimum_required(VERSION 3.21)
            project(pyyggdrasil_mpi_smoke CXX)

            set(CMAKE_CXX_STANDARD 20)
            set(CMAKE_CXX_STANDARD_REQUIRED ON)
            set(MPI_CXX_SKIP_MPICXX ON)
            find_package(MPI REQUIRED COMPONENTS CXX)
            find_package(Boost CONFIG REQUIRED COMPONENTS locale mpi serialization
                PATHS "${MPI_HOME}" NO_DEFAULT_PATH)
            find_package(TBB CONFIG REQUIRED PATHS "${MPI_HOME}" NO_DEFAULT_PATH)

            add_executable(mpi_smoke main.cpp)
            target_link_libraries(mpi_smoke PRIVATE
                MPI::MPI_CXX Boost::locale Boost::mpi Boost::serialization TBB::tbb)
            """
        ),
        encoding="utf-8",
    )
    (source_dir / "main.cpp").write_text(
        textwrap.dedent(
            """\
            #include <boost/mpi.hpp>
            #include <boost/locale.hpp>
            #include <boost/serialization/vector.hpp>
            #include <oneapi/tbb/task_arena.h>

            #include <vector>

            struct State
            {
                std::vector<int> values;

                template<typename Archive>
                void serialize(Archive& archive, unsigned)
                {
                    archive & values;
                }
            };

            int main(int argc, char** argv)
            {
                boost::mpi::environment environment(
                    argc, argv, boost::mpi::threading::multiple);
                boost::mpi::communicator world;
                oneapi::tbb::task_arena arena(1);
                arena.execute([] {});
                boost::locale::generator generator;
                const auto locale = generator("en_US.UTF-8");
                if (boost::locale::normalize("e\\xcc\\x81", boost::locale::norm_nfc, locale) != "\\xc3\\xa9") {
                    return 4;
                }
                if (boost::locale::fold_case("Stra\\xc3\\x9f" "e", locale) != "strasse") {
                    return 5;
                }
                if (
                    boost::mpi::environment::thread_level()
                        != boost::mpi::threading::multiple
                    || world.size() != 2
                ) {
                    return 1;
                }

                if (world.rank() == 0) {
                    world.send(1, 0, State {{1, 2, 3}});
                    int acknowledged = 0;
                    world.recv(1, 1, acknowledged);
                    return acknowledged == 1 ? 0 : 2;
                }

                State state;
                world.recv(0, 0, state);
                const int acknowledged = state.values == std::vector<int> {1, 2, 3};
                world.send(0, 1, acknowledged);
                return acknowledged == 1 ? 0 : 3;
            }
            """
        ),
        encoding="utf-8",
    )

    cmake = "cmake"
    subprocess.run(
        [
            cmake,
            "-S",
            str(source_dir),
            "-B",
            str(build_dir),
            f"-DCMAKE_PREFIX_PATH={prefix}",
            f"-DMPI_HOME={prefix}",
        ],
        check=True,
    )
    subprocess.run([cmake, "--build", str(build_dir)], check=True)
    subprocess.run(
        [mpiexec, "-n", "2", build_dir / "mpi_smoke"],
        check=True,
        timeout=60,
    )

    wrapper = subprocess.run(
        [mpicxx, "-show"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    assert str(prefix / "include") in wrapper
    assert str(prefix / "lib") in wrapper
    assert "dependencies-build" not in wrapper
    assert "dependencies-install" not in wrapper

    native_libraries = [
        *prefix.glob("lib*/libmpi*"),
        *prefix.glob("lib*/libboost_mpi*"),
        *prefix.glob("lib*/libboost_locale*"),
        *prefix.glob("lib*/libboost_serialization*"),
        *prefix.glob("lib*/libicu*"),
    ]
    for library in native_libraries:
        if platform.system() == "Darwin":
            metadata = subprocess.run(
                ["otool", "-L", library],
                check=True,
                capture_output=True,
                text=True,
            ).stdout
        else:
            metadata = subprocess.run(
                ["readelf", "-d", library],
                check=True,
                capture_output=True,
                text=True,
            ).stdout
        assert "dependencies-build" not in metadata
        assert "dependencies-install" not in metadata


def test_bundled_mpi(tmp_path: Path) -> None:
    _run_smoke_test(tmp_path)


def test_distribution_compliance() -> None:
    _check_distribution_compliance()


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="pyyggdrasil-mpi-") as tmp:
        _run_smoke_test(Path(tmp))
