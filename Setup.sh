
echo "setup-prototype.sh"
. /cvmfs/dune.opensciencegrid.org/spack/setup-env.sh
spack env activate dune-prototype
echo "Activated dune-prototype"

echo "load GCC so don't use system"
echo "GCC"
spack load gcc@12.5.0 arch=linux-almalinux9-x86_64_v2 
