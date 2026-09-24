module load python/3.12-26.1.0
export SPACK_PYTHON=$(which python3)
source /cvmfs/dune.opensciencegrid.org/spack/setup-env.sh
spack env activate dune-prototype
echo $SPACK_ENV
spack env status
spack find highfive
spack find hdf5