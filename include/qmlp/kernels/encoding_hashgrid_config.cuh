#pragma once

#include "common.cuh"

QUICKMLP_KERNEL_NAMESPACE_BEGIN

/**
 * How are the features of the individual layers combined?
 */
enum class LayerCombinationMode
{
    /**
     * The features are concatenated
     * --> #output features = #per-layer features * num layers
     */
    CONCAT,
    /**
     * The features are added together
     * --> #output features = #per-layer features
     */
     ADD
};
struct HashGridLayerConfig
{
    int memoryOffset;
    //DenseGrid (hashed=false) -> per-side resolution of the grid
    //HashGrid (hashed=true) -> number of elements in this grid
    int resolutionOrSize;
    bool hashed;
};
template<int NumDimensions, int NumLayers>
struct HashGridConfig
{
    HashGridLayerConfig layers[NumLayers];
    float* __restrict__ parametersForward;
    float* parametersBackward;
    StaticArray<float, NumDimensions> boundingBoxMin;
    StaticArray<float, NumDimensions> boundingBoxInvSize;
};


QUICKMLP_KERNEL_NAMESPACE_END