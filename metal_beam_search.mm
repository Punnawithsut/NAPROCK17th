#include "metal_beam_search.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstring>
#include <iostream>

struct MetalBeamSearch::Impl {
  id<MTLDevice> device;
  id<MTLCommandQueue> commandQueue;
  id<MTLComputePipelineState> computePipeline;

  id<MTLBuffer> gridBuffer;
  id<MTLBuffer> rotationBuffer;
  id<MTLBuffer> zobristBuffer;
  id<MTLBuffer> resultBuffer;

  int max_states;
  int max_rotations;
  int max_n;
};

MetalBeamSearch::MetalBeamSearch(int max_n, int max_states, int max_rotations) {
  impl = std::make_unique<Impl>();
  impl->max_n = max_n;
  impl->max_states = max_states;
  impl->max_rotations = max_rotations;

  // 1. Get default device
  impl->device = MTLCreateSystemDefaultDevice();
  if (!impl->device) {
    std::cerr << "Metal is not supported on this device" << std::endl;
    exit(1);
  }

  impl->commandQueue = [impl->device newCommandQueue];

  // 2. Load shader logic
  NSError *error = nil;
  NSString *shaderSource =
      [NSString stringWithContentsOfFile:@"beam_search_kernels.metal"
                                encoding:NSUTF8StringEncoding
                                   error:&error];
  if (error) {
    std::cerr << "Failed to read shader file: " <<
        [[error localizedDescription] UTF8String] << std::endl;
    exit(1);
  }

  MTLCompileOptions *options = [MTLCompileOptions new];
  options.fastMathEnabled = YES;

  id<MTLLibrary> library = [impl->device newLibraryWithSource:shaderSource
                                                      options:options
                                                        error:&error];
  if (error) {
    std::cerr << "Failed to compile shader: " <<
        [[error localizedDescription] UTF8String] << std::endl;
    exit(1);
  }

  id<MTLFunction> function =
      [library newFunctionWithName:@"evaluate_rotations"];
  if (!function) {
    std::cerr << "Failed to find function 'evaluate_rotations'" << std::endl;
    exit(1);
  }

  impl->computePipeline =
      [impl->device newComputePipelineStateWithFunction:function error:&error];
  if (error) {
    std::cerr << "Failed to create pipeline state: " <<
        [[error localizedDescription] UTF8String] << std::endl;
    exit(1);
  }

  // 3. Allocate buffers (Shared memory for Unified Memory Architecture)
  NSUInteger gridSizeBytes = max_states * max_n * max_n * sizeof(uint16_t);
  impl->gridBuffer =
      [impl->device newBufferWithLength:gridSizeBytes
                                options:MTLResourceStorageModeShared];

  // (k, i, j) -> 3 ints -> 12 bytes. But shader uses struct Rotation {int k;
  // int i; int j;} which is 12 bytes.
  NSUInteger rotSizeBytes = max_rotations * 3 * sizeof(int);
  impl->rotationBuffer =
      [impl->device newBufferWithLength:rotSizeBytes
                                options:MTLResourceStorageModeShared];

  // Zobrist table is 64 * 64 * 2048 * 8 = 67,108,864 bytes (~64MB)
  NSUInteger zobristSizeBytes = 64 * 64 * 2048 * sizeof(uint64_t);
  impl->zobristBuffer =
      [impl->device newBufferWithLength:zobristSizeBytes
                                options:MTLResourceStorageModeShared];

  NSUInteger resultSizeBytes = max_states * max_rotations * sizeof(GPUResult);
  impl->resultBuffer =
      [impl->device newBufferWithLength:resultSizeBytes
                                options:MTLResourceStorageModeShared];
}

MetalBeamSearch::~MetalBeamSearch() {
  // ARC cleans up Obj-C objects, unique_ptr cleans up Impl
}

void MetalBeamSearch::set_zobrist_table(const uint64_t *flat_zobrist_table) {
  void *ptr = [impl->zobristBuffer contents];
  memcpy(ptr, flat_zobrist_table, 64 * 64 * 2048 * sizeof(uint64_t));
}

std::vector<GPUResult> MetalBeamSearch::evaluate_batch(
    const std::vector<uint16_t> &grids_flat, int num_states,
    const std::vector<std::tuple<int, int, int>> &rotations, int grid_size) {
  if (num_states == 0 || rotations.empty())
    return {};

  int num_rotations = rotations.size();

  // Check bounds
  if (num_states > impl->max_states || num_rotations > impl->max_rotations) {
    std::cerr << "Batch bounds exceeded! states: " << num_states << "/"
              << impl->max_states << ", rots: " << num_rotations << "/"
              << impl->max_rotations << std::endl;
    return {};
  }

  // 1. Copy data to buffers (unified memory means this is right into shared
  // RAM, very fast)
  memcpy([impl->gridBuffer contents], grids_flat.data(),
         grids_flat.size() * sizeof(uint16_t));

  int *rot_ptr = (int *)[impl->rotationBuffer contents];
  for (int idx = 0; idx < num_rotations; ++idx) {
    rot_ptr[idx * 3 + 0] = std::get<0>(rotations[idx]);
    rot_ptr[idx * 3 + 1] = std::get<1>(rotations[idx]);
    rot_ptr[idx * 3 + 2] = std::get<2>(rotations[idx]);
  }

  // 2. Setup command buffer
  id<MTLCommandBuffer> commandBuffer = [impl->commandQueue commandBuffer];
  id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];

  [encoder setComputePipelineState:impl->computePipeline];
  [encoder setBuffer:impl->gridBuffer offset:0 atIndex:0];
  [encoder setBuffer:impl->rotationBuffer offset:0 atIndex:1];
  [encoder setBuffer:impl->zobristBuffer offset:0 atIndex:2];
  [encoder setBuffer:impl->resultBuffer offset:0 atIndex:3];

  [encoder setBytes:&grid_size length:sizeof(int) atIndex:4];
  [encoder setBytes:&num_states length:sizeof(int) atIndex:5];
  [encoder setBytes:&num_rotations length:sizeof(int) atIndex:6];

  // 3. Dispatch threads
  int total_threads = num_states * num_rotations;
  MTLSize gridSize = MTLSizeMake(total_threads, 1, 1);

  NSUInteger threadGroupSize =
      impl->computePipeline.maxTotalThreadsPerThreadgroup;
  if (threadGroupSize > (NSUInteger)total_threads) {
    threadGroupSize = total_threads;
  }
  MTLSize tgs = MTLSizeMake(threadGroupSize, 1, 1);

  [encoder dispatchThreads:gridSize threadsPerThreadgroup:tgs];
  [encoder endEncoding];

  // 4. Commit and wait
  [commandBuffer commit];
  [commandBuffer waitUntilCompleted];

  // 5. Read back results
  GPUResult *res_ptr = (GPUResult *)[impl->resultBuffer contents];
  std::vector<GPUResult> results(total_threads);
  memcpy(results.data(), res_ptr, total_threads * sizeof(GPUResult));

  return results;
}
