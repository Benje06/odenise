// backend_cuda.cu -- backend de calcul CUDA (STUB minimal, etape 1).
// Contenu reel (FFT cuFFT, kernels de gain) ajoute a l'etape backend GPU.
//
// nvcc refuse un .cu vide ("a single input file is required") : ce stub
// fournit une unite de compilation valide en attendant le vrai code.

#include <cuda_runtime.h>

namespace ns::cuda {

// Renvoie le nombre de devices CUDA visibles (sanity check de la chaine).
int deviceCount() noexcept {
    int n = 0;
    if (cudaGetDeviceCount(&n) != cudaSuccess)
        return 0;
    return n;
}

} // namespace ns::cuda
