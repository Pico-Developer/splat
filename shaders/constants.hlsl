/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

/**
 * Global defines controlling the radius of each splat. This is not a scaling
 * factor, but a limit to how far from the center of each Gaussian we will
 * evaluate.
 *
 * Most 3DGS viewers seem to be using a sqrt(8)σ radius, so that's a good
 * default.
 */
#define RADIUS_SIGMA sqrt(8)
#define RADIUS_SIGMA_OVER_SQRT_2 (RADIUS_SIGMA / sqrt(2))
#define CUTOFF_RADIUS_SIGMA_SQUARED_OVER_2 \
  (RADIUS_SIGMA_OVER_SQRT_2 * RADIUS_SIGMA_OVER_SQRT_2)

/**
 * Precision with which to sort splats, when using GPU sorting pipeline.
 */
#define DISTANCE_PRECISION 16
#define DISTANCE_SCALE half((1 << DISTANCE_PRECISION) - 2)
#define DISTANCE_NOT_VISIBLE ((1 << DISTANCE_PRECISION) - 1)