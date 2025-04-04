/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

/**
 * Extract a float16 packed into offset.
 * Assumes compiler is configured to output half-precision floats, else
 * will be promoted to float32.
 *
 * Note: Confirmed via RenderDoc that this compiles down to a single shift and
 * at most one mask (excluded when signed and offset is 0).
 *
 * @param s - Number of sign bits in packed type.
 * @param e - Number of exponent bits.
 * @param m - Number of significand bits.
 * @param packed - uint storing packed float.
 * @param offset - First lowest bit holding the value to unpack, of size s+e+m.
 * @return half containing the extracted value.
 */
half unpack_f16(uint s, uint e, uint m, uint packed, uint offset) {
  uint shift = 15 - e - m;
  uint mask = ((1 << (s + e + m)) - 1) << shift;
  if (s == 1 && offset == 0) {
    return f16tof32(packed << shift);
  } else if (offset < shift) {
    return f16tof32((packed << (shift - offset)) & mask);
  } else {
    return f16tof32((packed >> (offset - shift)) & mask);
  }
}

/**
 * Extracted an unsigned, normalized integer from bits in offset, and return it
 * as its unnormalized value in a float16 (if compiler configured correctly).
 *
 * @param bits - Number of bits comprising packed value.
 * @param packed - uint storing packed UNorm.
 * @param offset - First lowest bit holding the value to unpack, of size bits.
 * @return half containing unnormalized value equal to the packed UNorm, when
 * interpreted as an integer.
 */
half unpack_unorm(uint bits, uint packed, uint offset) {
  uint Mask = (1 << bits) - 1;
  if (offset == 0) {
    return half(packed & Mask);
  } else {
    return half((packed >> offset) & Mask);
  }
}

/**
 * Extracts the 3x3 covariance matrixes packed into packedCovMat.
 *
 * @param packedCovMat - Holds packed covariance matrix.
 * @return half3x3 holding extracted covariance matrix, fully populated (not
 * upper triangular).
 */
half3x3 unpack_cov_mat(uint2 packedCovMat) {
  half xx = unpack_f16(0, 5, 5, packedCovMat.y, 22);
  half xy = unpack_f16(1, 5, 5, packedCovMat.y, 11);
  half xz = unpack_f16(1, 5, 5, packedCovMat.y, 0);
  half yy = unpack_f16(0, 5, 5, packedCovMat.x, 22);
  half yz = unpack_f16(1, 5, 5, packedCovMat.x, 11);
  half zz = unpack_f16(0, 5, 6, packedCovMat.x, 0);

  return half3x3(xx, xy, xz, xy, yy, yz, xz, yz, zz);
}

/**
 * Extracts the position from packed, where each channel has been normalized and
 * offset.
 *
 * @param packed - uint holding x11y11z10 position.
 * @param scale - Scaling factor. To avoid extra shader ops, this is (per
 * channel): (PosMax - PosMin) * 100cm / UNormMax
 * @param offset - The origin that all packed positions are relative to.
 * @return half containing unpacked (x, y, z).
 */
half4 unpack_pos(uint packed, half3 scale, half3 offset) {
  half x = unpack_unorm(11, packed, 0);
  half y = unpack_unorm(11, packed, 11);
  half z = unpack_unorm(10, packed, 22);

  return half4(half3(x, y, z) * scale + offset, 1.f);
}