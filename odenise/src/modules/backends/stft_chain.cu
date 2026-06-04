// stft_chain.cu -- chaine STFT GPU (STUB minimal, etape 1).
// Pipeline reel (fenetrage -> cuFFT -> gain par bande -> iFFT -> overlap-add,
// un stream par piste) ajoute a l'etape STFT.
//
// Stub valide pour que nvcc ait une unite de compilation non vide.

#include <cuda_runtime.h>

namespace ns::cuda {

// Placeholder : sera remplace par le lancement de la chaine STFT par stream.
void stftChainNoop() noexcept {}

} // namespace ns::cuda
