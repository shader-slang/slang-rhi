#pragma once

// Neural graphics primitive settings shared by C++ and Slang.
// Keep the input/output widths even for FP16 packing.
#define NGP_GRID_LEVELS 8
#define NGP_GRID_MIN_RESOLUTION 8
#define NGP_GRID_MAX_RESOLUTION 256
#define NGP_GRID_MAX_ENTRIES 4096
#define NGP_GRID_FEATURES 2
#define NGP_INPUTS (NGP_GRID_FEATURES * NGP_GRID_LEVELS)
#define NGP_WIDTH 64
#define NGP_OUTPUTS 4
#define NGP_BATCH_SIZE 128
#define NGP_STEPS_PER_FRAME 4
#define NGP_LEARNING_RATE 0.002f
#define NGP_GRID_LEARNING_RATE 0.01f
// Measure fine vein errors independently of the random training samples.
#define NGP_VALIDATION_SIZE 128
#define NGP_PREVIEW_SIZE 384

#define NGP_W0 0
#define NGP_B0 (NGP_W0 + NGP_INPUTS * NGP_WIDTH)
#define NGP_W1 (NGP_B0 + NGP_WIDTH)
#define NGP_B1 (NGP_W1 + NGP_WIDTH * NGP_WIDTH)
#define NGP_W2 (NGP_B1 + NGP_WIDTH)
#define NGP_B2 (NGP_W2 + NGP_WIDTH * NGP_OUTPUTS)
#define NGP_PARAMETER_COUNT (NGP_B2 + NGP_OUTPUTS)
