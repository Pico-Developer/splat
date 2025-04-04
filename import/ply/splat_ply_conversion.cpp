/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#include "splat_ply_conversion.h"

#include "import/splat_logging.h"

namespace import::ply {
bool validate_metadata(const Metadata& metadata) {
  std::vector<Property> required_properties{
      Property::X,         Property::Y,         Property::Z,
      Property::RotationX, Property::RotationY, Property::RotationZ,
      Property::RotationW, Property::ScaleX,    Property::ScaleY,
      Property::ScaleZ,    Property::DCRed,     Property::DCGreen,
      Property::DCBlue,    Property::Opacity,
  };

  for (auto&& property : required_properties) {
    if (!metadata.properties.contains(property)) {
      log_error("Required property %u missing.",
                static_cast<uint32_t>(property));
      return false;
    }
  }

  return true;
}
}  // namespace import::ply