/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#pragma once

#include <algorithm>
#include <functional>
#include <span>
#include <unordered_map>
#include <variant>

namespace import {

/**
 * Types that may appear in a 3DGS file (e.g. `.ply`).
 */
enum class Property {
  Ignore,
  X,
  Y,
  Z,
  RotationX,
  RotationY,
  RotationZ,
  RotationW,
  ScaleX,
  ScaleY,
  ScaleZ,
  DCRed,
  DCGreen,
  DCBlue,
  Opacity
};

/**
 * Property encodings that may appear in a 3DGS file (e.g. `.ply`).
 *
 * This is separate from `PropertyType` as these types may not be 1-to-1 with
 * their C++ representation (e.g. due to endianness), but this isn't strictly
 * necessary.
 *
 * The abstractions used here for parsing were designed early based on guesses
 * as to where and how flexibility will be needed for future formats. Expect
 * these to need changes when other formats are added, particularly those with
 * packed vectors (e.g. 11/11/10) or LUTs.
 */
enum class PropertyFormat { Unknown, I8, I16, I32, U8, U16, U32, F32, F64 };

/**
 * Variant for any recognizable data type in a 3DGS splat asset.
 */
typedef std::variant<int8_t, int16_t, int32_t, uint8_t, uint16_t, uint32_t,
                     float, double>
    PropertyType;

/**
 * Defines the available properties and their formats in a 3DGS file, as well as
 * the total number of splats.
 */
struct Metadata {
  std::unordered_map<Property, PropertyFormat> properties;
  size_t num_splats;
};

/**
 * Convert value extracted from a 3DGS file to a specified type.
 *
 * @param Value - Raw value.
 * @return Value, converted to T.
 */
template <typename T>
inline T to(PropertyType value) {
  return std::visit([](auto&& value) { return static_cast<T>(value); }, value);
}

/**
 * Convert degree-0 spherical harmonic coefficients (DC) to an 8-bit color.
 * Note: Alpha does **not** use the same formula; please use `to_alpha_linear`
 * instead.
 *
 * @see
 * https://github.com/mkkellogg/GaussianSplats3D/issues/47#issuecomment-1801360116
 *
 * @param dc - Raw DC value.
 * @return Linear, 8-bit color channel.
 */
inline uint8_t to_color_linear(PropertyType dc) {
  float dc_f = to<float>(dc);
  float color_srgb = .5f + .2820948f * dc_f;

  /**
   * HACK(seth): While I haven't confirmed this, I believe the proper
   * implementation would do all blending in sRGB space.
   * However, as we are only able to inject into stages of the rendering
   * pipeline in linear space (prior to tonemapping or automatic sRGB
   * conversion), we have to convert to linear space. If not, the result will
   * be too bright from double application of gamma correction.
   */
  float color_linear = std::pow(color_srgb, 2.2f);
  return static_cast<uint8_t>(std::clamp(color_linear * 255.f, 0.f, 255.f));
}

/**
 * Convert opacity extracted from a 3DGS file to an 8-bit alpha value.
 * Assumes an inverse logistic encoding (https://en.wikipedia.org/wiki/Logit).
 *
 * @param opacity - Raw opacity.
 * @return Linear, 8-bit alpha.
 */
inline uint8_t to_alpha_linear(PropertyType opacity) {
  float opacity_f = to<float>(opacity);
  return static_cast<uint8_t>(
      std::clamp(1.f / (1.f + exp(-opacity_f)) * 255.f, 0.f, 255.f));
}

/**
 * Converts a scaling factor, extracted from a 3DGS file, to linear float32.
 * Assumes input was logarithmic.
 *
 * @param Scale - Raw log scale.
 * @return Linear, float scale.
 */
inline float to_scale_linear(PropertyType scale) {
  return exp(to<float>(scale));
}

/**
 * Function wrapper which will be passed to `ParseSplatFn`, in order to get the
 * `PropertyFormat` and value of a provided `Property`.
 */
typedef std::function<PropertyType(Property)> GetPropertyFn;

/**
 * Function type that should be implemented to parse an individual splat from a
 * 3DGS file.
 *
 * This function will receive a `uint64_t` indicating the index of the current
 * splat, and a callable `GetPropertyFn` that should be called to get the raw
 * data for the splat.
 *
 * Per property the parser is interested in, `GetPropertyFn` should be called to
 * get the type and value of the property. This can then be converted and stored
 * however is preferred.
 */
typedef std::function<void(uint64_t, GetPropertyFn)> ParseSplatFn;

/**
 * Interface to 3DGS file parsers.
 * Makes importer easily extensible to future file types.
 */
class ISplatParser {
 public:
  /**
   * Reads only the 3DGS metadata from the given buffer. This lets the caller
   * configure how it will convert the data before calling `parse_data` (e.g.
   * whether to using `float` or `U8` types for color).
   *
   * @param buffer - A view of a buffer of 3DGS data.
   * @param metadata - Upon success, contains the metadata of the splat asset.
   * @return Whether the parser was able to successfully decode the metadata.
   */
  virtual bool parse_metadata(std::span<const uint8_t> buffer,
                              Metadata& metadata) = 0;

  /**
   * Parses 3DGS using the provided function. The caller should configure
   * `parse_splat` based on the available metadata, to perform whatever
   * conversions are necessary.
   *
   * @param parse_splat - This function will be repeatedly called by
   * `parse_data`, to read and convert the splat data.
   * @return Whether the parser was able to successfully decode the 3DGS data.
   */
  virtual bool parse_data(ParseSplatFn parse_splat) = 0;
};
}  // namespace import