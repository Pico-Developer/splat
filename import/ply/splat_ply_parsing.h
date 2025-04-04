/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#pragma once

#include "import/splat_parsing.h"

namespace import::ply {
/**
 * `ply` data encoding.
 */
enum class PlyFormat { Invalid, ASCII, BinaryBigEndian, BinaryLittleEndian };

/**
 * Type and location within file of a property.
 */
struct PropertyDesc {
  uint64_t offset = 0;
  PropertyFormat type = PropertyFormat::Unknown;
};

/**
 * Parser for `.ply` 3DGS assets.
 *
 * @see https://gamma.cs.unc.edu/POWERPLANT/papers/ply.pdf
 */
class SplatParserPly final : public ISplatParser {
 public:
  //~ Begin ISplatParser Interface
  SPLAT_EXPORT_API virtual bool parse_metadata(
      std::span<const uint8_t> ply_buffer, Metadata& metadata) override;
  SPLAT_EXPORT_API virtual bool parse_data(ParseSplatFn parse_splat);
  //~ End ISplatParser Interface

 private:
  /**
   * Adds property to the list detected in the file.
   *
   * @param property - The property (e.g. x position).
   * @param type - The type of the property (e.g. float).
   * @return If property added successfully.
   */
  bool add_property(Property property, PropertyFormat type);

  /**
   * Parses file header.
   * @return Whether successful.
   */
  bool parse_header();

  PlyFormat format = PlyFormat::Invalid;
  std::unordered_map<Property, PropertyDesc> layout;
  size_t num_splats = 0;
  size_t splat_size = 0;
  std::span<const uint8_t> buffer;
};
}  // namespace import::ply