/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#pragma once

#include "import/splat_parsing.h"

namespace import::ply {
/**
 * This `.ply` importer, as written today, requires:
 * - Position
 * - Rotation
 * - Scale
 * - DC Spherical Harmonics (i.e. solid color)
 * - Opacity
 *
 * Non-0-degree SH's are not implemented.
 *
 * @param metadata - Metadata extracted from file, which will be checked for
 * compatibility.
 * @return Whether file can be imported by this importer.
 */
SPLAT_EXPORT_API bool validate_metadata(const Metadata& metadata);

/**
 * Extracts and converts the raw splat data from a 3DGS asset.
 *
 * @param index - Index of the splat to extract. Passed to `get`.
 * @param get - Callable used to extract raw data at `index`.
 * @param positions - View of array to write position.
 * @param rotations - View of array to write rotation.
 * @param scales - View of array to write scale.
 * @param colors - View of array to write colors.
 */
template <typename F3, typename F4, typename RGBA>
void convert_splat(uint64_t index, GetPropertyFn get,
                   const std::span<F3>& positions,
                   const std::span<F4>& rotations, const std::span<F3>& scales,
                   const std::span<RGBA>& colors) {
  /**
   * Position.
   *
   * Note: Axes converted as follows:
   * Input:  Z+ forward, X+ right, Y- up
   * Output: X+ forward, Y+ right, Z+ up
   */
  positions[index] =
      F3(to<float>(get(Property::Z)), to<float>(get(Property::X)),
         -to<float>(get(Property::Y)));

  /**
   * Covariance (scaling & rotation).
   *
   * As we're swapping handedness, all imaginary parts of the quaternion must
   * be negated.
   */
  float x = -to<float>(get(Property::RotationZ));
  float y = -to<float>(get(Property::RotationX));
  float z = to<float>(get(Property::RotationY));  // -1 * -Y
  float w = to<float>(get(Property::RotationW));

  float len = std::sqrt(x * x + y * y + z * z + w * w);

  rotations[index] = F4(x / len, y / len, z / len, w / len);

  /**
   * Scaling.
   * Note that sign doesn't matter here.
   */
  scales[index] = F3(to_scale_linear(get(Property::ScaleZ)),
                     to_scale_linear(get(Property::ScaleX)),
                     to_scale_linear(get(Property::ScaleY)));

  /**
   * Color.
   */
  colors[index] = RGBA(to_color_linear(get(Property::DCRed)),
                       to_color_linear(get(Property::DCGreen)),
                       to_color_linear(get(Property::DCBlue)),
                       to_alpha_linear(get(Property::Opacity)));
}
}  // namespace import::ply