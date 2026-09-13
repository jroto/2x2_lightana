#!/usr/bin/env bash
# Setup environment in NERSC
# Usage:
#   source Setup.sh
#   root -l macros/example_vbr.C
#

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "Error: source this script; do not execute it:"
    echo "  source Setup.sh"
    exit 1
fi

echo "Activating DUNE Spack environment"

source /cvmfs/dune.opensciencegrid.org/spack/setup-env.sh
spack env activate dune-prototype

# Use the compiler defined for this DUNE Spack environment.
spack load gcc@12.5.0 arch=linux-almalinux9-x86_64_v2

# HighFive is header-only, but HDF5 provides headers and shared libraries.
spack load hdf5

export HIGHFIVE_ROOT="$(spack location -i highfive)"
export HDF5_ROOT="$(spack location -i hdf5)"

if [[ ! -f "${HIGHFIVE_ROOT}/include/highfive/H5File.hpp" ]]; then
    echo "ERROR: HighFive header not found:"
    echo "  ${HIGHFIVE_ROOT}/include/highfive/H5File.hpp"
    return 1
fi

if [[ ! -f "${HDF5_ROOT}/include/hdf5.h" ]]; then
    echo "ERROR: HDF5 header not found:"
    echo "  ${HDF5_ROOT}/include/hdf5.h"
    return 1
fi

# ROOT/Cling uses ROOT_INCLUDE_PATH when it interprets or ACLiC-compiles
# a macro. This resolves <highfive/H5File.hpp> and <hdf5.h>.
export ROOT_INCLUDE_PATH="${HIGHFIVE_ROOT}/include:${HDF5_ROOT}/include${ROOT_INCLUDE_PATH:+:${ROOT_INCLUDE_PATH}}"

# Locate the HDF5 shared library supplied by the same Spack environment.
if [[ -f "${HDF5_ROOT}/lib/libhdf5.so" ]]; then
    export HDF5_LIBRARY="${HDF5_ROOT}/lib/libhdf5.so"
elif [[ -f "${HDF5_ROOT}/lib64/libhdf5.so" ]]; then
    export HDF5_LIBRARY="${HDF5_ROOT}/lib64/libhdf5.so"
else
    echo "ERROR: Could not locate libhdf5.so below ${HDF5_ROOT}"
    return 1
fi

# Make the library directory discoverable at runtime.
export LD_LIBRARY_PATH="$(dirname "${HDF5_LIBRARY}")${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

# ROOT's interpreter compiles the macro but does not automatically link
# HDF5 merely because its headers were included. Preload the DUNE-Spack
# HDF5 library so ordinary `root -l macro.C` resolves H5* symbols.
case " ${LD_PRELOAD:-} " in
    *" ${HDF5_LIBRARY} "*) ;;
    *) export LD_PRELOAD="${HDF5_LIBRARY}${LD_PRELOAD:+ ${LD_PRELOAD}}" ;;
esac

echo "2x2_lightana environment ready"
echo "  ROOT:     $(root-config --version)"
echo "  HighFive: ${HIGHFIVE_ROOT}"
echo "  HDF5:     ${HDF5_ROOT}"
echo "  HDF5 lib: ${HDF5_LIBRARY}"