
echo "Setup for dunegpvm environment"
. /cvmfs/dune.opensciencegrid.org/spack/setup-env.sh
spack env activate dune-prototype
echo "Activated dune-prototype"

echo "load GCC so don't use system"
echo "GCC"
spack load gcc@12.5.0 arch=linux-almalinux9-x86_64_v2 


export HIGHFIVE_ROOT="$(spack location -i highfive)"
ls -l "${HIGHFIVE_ROOT}/include/highfive/H5File.hpp"

export HDF5_ROOT="$(spack location -i hdf5)"

export ROOT_INCLUDE_PATH="${HIGHFIVE_ROOT}/include:${HDF5_ROOT}/include${ROOT_INCLUDE_PATH:+:${ROOT_INCLUDE_PATH}}"

echo "${ROOT_INCLUDE_PATH}"