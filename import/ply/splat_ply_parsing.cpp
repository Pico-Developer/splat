/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

#include "splat_ply_parsing.h"

#include <bit>
#include <charconv>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "import/splat_logging.h"

namespace import::ply {
namespace {
/**
 * Maps "format" to `PlyFormat::X`.
 */
#define FMT(s, f) {#s, PlyFormat::f}
const std::unordered_map<std::string_view, PlyFormat> format_map{
    FMT(ascii, ASCII), FMT(binary_big_endian, BinaryBigEndian),
    FMT(binary_little_endian, BinaryLittleEndian)};
#undef FMT

/**
 * Maps "property" to `Property::X`.
 */
#define PROP(s, p) {#s, Property::p}
const std::unordered_map<std::string_view, Property> property_map{
    PROP(x, X),
    PROP(y, Y),
    PROP(z, Z),
    PROP(f_dc_0, DCRed),
    PROP(f_dc_1, DCGreen),
    PROP(f_dc_2, DCBlue),
    PROP(opacity, Opacity),
    PROP(rot_0, RotationW),
    PROP(rot_1, RotationX),
    PROP(rot_2, RotationY),
    PROP(rot_3, RotationZ),
    PROP(scale_0, ScaleX),
    PROP(scale_1, ScaleY),
    PROP(scale_2, ScaleZ)};
#undef PROP

/**
 * Maps "type" to `PropertyFormat::X`.
 */
#define TYPE(s, t) {#s, PropertyFormat::t}
const std::unordered_map<std::string_view, PropertyFormat> type_map{
    TYPE(float, F32), TYPE(float32, F32)};
#undef TYPE

/**
 * Maps `PropertyFormat::X` to the size of the type in bytes.
 */
#define SIZE(t, s) {PropertyFormat::t, s}
const std::unordered_map<PropertyFormat, size_t> type_size_map{SIZE(F32, 4)};
#undef SIZE

const char* const whitespace_chars = " \t";

/**
 * Gets the next line, delimited by '\n', and advances the `text` view.
 *
 * @param text - The text to search. Will be moved forward to one character past
 * the end of the found line (i.e. 1 after '\n').
 * @return A view to the extracted line excluding '\n' and any preceeding or
 * following whitespace, if found, else `nullopt`.
 */
std::optional<std::string_view> pop_line(std::string_view& text) {
  size_t eol_idx = text.find('\n');
  if (eol_idx == std::string_view::npos) {
    return std::move(text);  // Clears text.
  }

  // Excludes trailing `\n`, if present.
  std::string_view line_out(text.begin(), text.begin() + eol_idx);

  // Remove leading whitespace.
  size_t start_idx = line_out.find_first_not_of(whitespace_chars);
  // No token found in text.
  if (start_idx == std::string_view::npos) {
    return std::nullopt;
  }
  line_out.remove_prefix(start_idx);

  // Remove trailing whitespace.
  size_t end_idx = line_out.find_last_not_of(whitespace_chars);
  line_out.remove_suffix(line_out.size() - 1 - end_idx);

  // Remove line, including `\n`, from text.
  text.remove_prefix(eol_idx + 1);

  return line_out;
}

/**
 * Gets next token, delimited by whitespace, and advances the `line` view.
 *
 * @param line - The text to search. Will be moved forward to one character past
 * the end of the whitespace (i.e. first non-whitespace).
 * @return The first found token, with preceding and trailing whitespace removed
 * if found, else a `nullopt`.
 */
std::optional<std::string_view> pop_token(std::string_view& line) {
  // Check the line is configured to correctly start on the token boundary.
  size_t start_idx = line.find_first_not_of(whitespace_chars);
  if (start_idx != 0) {
    return std::nullopt;
  }

  // Find end of token.
  size_t whitespace_idx = line.find_first_of(whitespace_chars);

  // If end of line, return entirety.
  if (whitespace_idx == std::string_view::npos) {
    return std::move(line);  // Clears line.
  }

  std::string_view token(line.begin(), line.begin() + whitespace_idx);

  // Move line to start of next token, skipping whitespace.
  size_t next_token_idx =
      line.find_first_not_of(whitespace_chars, whitespace_idx);
  line.remove_prefix(next_token_idx);

  return token;
}

template <typename T, std::endian E>
T convert(const void* data) {
  // <https://en.cppreference.com/w/cpp/types/endian>.
  static_assert(std::endian::native == std::endian::big ||
                    std::endian::native == std::endian::little,
                "System must not be mixed endian.");

  if constexpr (E == std::endian::native) {
    return *static_cast<const T*>(data);
  } else {
    T swapped;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint8_t* swapped_bytes = reinterpret_cast<uint8_t*>(&swapped);

    for (size_t i = 0; i < sizeof(T); ++i) {
      swapped_bytes[i] = bytes[sizeof(T) - 1 - i];
    }

    return swapped;
  }
}

template <std::endian E>
PropertyType read_binary(const void* data, PropertyFormat type) {
  switch (type) {
    case PropertyFormat::F32: {
      return convert<float, E>(data);
    }
    default: {
      log_warn("Unexpected type. Unable to convert.");
      return 0.f;
    }
  }
}

template <std::endian E>
PropertyType get_property_binary(
    Property property, const std::unordered_map<Property, PropertyDesc>& layout,
    const uint8_t* data) {
  const PropertyDesc& desc = layout.at(property);
  return read_binary<E>(&data[desc.offset], desc.type);
}

}  // namespace

bool SplatParserPly::add_property(Property property, PropertyFormat type) {
  if (property != Property::Ignore) {
    if (layout.contains(property)) {
      return false;
    }
    layout.emplace(property, PropertyDesc{splat_size, type});
  }
  splat_size += type_size_map.at(type);
  return true;
}

bool SplatParserPly::parse_header() {
  std::string_view text(reinterpret_cast<const char*>(buffer.data()),
                        buffer.size());

  // `ply`.
  std::optional<std::string_view> line;
  line = pop_line(text);
  if (!line) {
    log_error("Unable to parse magic number.");
    return false;
  }
  if (*line != "ply") {
    log_error("Invalid magic number: %.*s.", line->size(), line->data());
    return false;
  }

  // `format <encoding> 1.0`.
  line = pop_line(text);
  if (!line) {
    log_error("Unable to parse format line.");
    return false;
  }

  // `format`.
  std::optional<std::string_view> token;
  token = pop_token(*line);
  if (!token) {
    log_error("Unexpected format metadata: %.*s.", line->size(), line->data());
    return false;
  }
  if (*token != "format") {
    log_error("Invalid format metadata: %.*s.", line->size(), line->data());
    return false;
  }

  // `<encoding>`.
  std::optional<std::string_view> format_token;
  format_token = pop_token(*line);
  if (!format_token) {
    log_error("Unable to parse format type: %.*s.", line->size(), line->data());
    return false;
  }
  if (!format_map.contains(*format_token)) {
    log_error("Invalid format type: %.*s.", format_token->size(),
              format_token->data());
    return false;
  }
  format = format_map.at(*format_token);

  // `1.0`.
  token = pop_token(*line);
  if (!token) {
    log_error("Unable to parse format version: %.*s.", line->size(),
              line->data());
    return false;
  }
  if (*token != "1.0") {
    log_warn("Unexpected encoding version %.*s for %.*s. Continuing anyway.",
             token->size(), token->data(), format_token->size(),
             format_token->data());
  }

  // Iterate until we hit `end_header`.
  while (true) {
    line = pop_line(text);
    if (!line) {
      log_error("Unable to parse header line.");
      return false;
    }
    token = pop_token(*line);
    if (!token) {
      log_error("Invalid header line: %.*s.", line->size(), line->data());
      return false;
    }

    // `comment <comment>`.
    if (*token == "comment") {
      continue;
    }

    // `element vertex <count>`.
    else if (*token == "element") {
      // Must have a single `element vertex <count>` in file.
      if (num_splats != 0) {
        log_error(
            "Unable to import `.ply` with more than one vertex "
            "element specified.");
        return false;
      }
      token = pop_token(*line);
      if (!token) {
        log_error("Invalid vertex element line: %.*s.", line->size(),
                  line->data());
        return false;
      }
      if (*token != "vertex") {
        log_error("Unexpected element type: %.*s.", token->size(),
                  token->data());
        return false;
      }

      // `<count>`.
      token = pop_token(*line);
      if (!token) {
        log_error("Invalid vertex element count: %.*s.", line->size(),
                  line->data());
        return false;
      }
      std::from_chars_result res =
          std::from_chars(std::to_address(token->begin()),
                          std::to_address(token->end()), num_splats);
      if (res.ec != std::errc{}) {
        log_error("Failed to parse vertex count: %.*s.", token->size(),
                  token->data());
        return false;
      } else if (num_splats == 0) {
        log_error("Found zero splats. Stopping.");
        return false;
      }
    }

    // `property <type> <name>`.
    else if (*token == "property") {
      if (num_splats == 0) {
        log_error("Invalid property line (missing associated element): %.*s.",
                  line->size(), line->data());
        return false;
      }

      // `<type>`.
      token = pop_token(*line);
      if (!token) {
        log_error("Unable to parse property type: %.*s.", line->size(),
                  line->data());
        return false;
      }
      if (!type_map.contains(*token)) {
        log_error("Invalid property type: %.*s.", line->size(), line->data());
        return false;
      }
      PropertyFormat type = type_map.at(*token);

      // `<name>`.
      token = pop_token(*line);
      if (!token) {
        log_error("Unable to parse property name: %.*s.", line->size(),
                  line->data());
        return false;
      }
      if (property_map.contains(*token)) {
        Property prop = property_map.at(*token);
        if (!add_property(prop, type)) {
          log_error("Duplicate property: %.*s", token->size(), token->data());
          return false;
        }
      } else {
        add_property(Property::Ignore, type);
      }
    }

    // `end_header`.
    else if (*token == "end_header") {
      std::ptrdiff_t offset =
          reinterpret_cast<const uint8_t*>(std::to_address(text.begin())) -
          std::to_address(buffer.begin());
      buffer = buffer.subspan(offset);
      return true;
    }

    // Unknown or invalid line in header.
    else {
      log_error("Unknown header element: %.*s", token->size(), token->data());
      return false;
    }
  }
}

bool SplatParserPly::parse_metadata(std::span<const uint8_t> ply_buffer,
                                    Metadata& metadata) {
  buffer = ply_buffer;

  if (!parse_header()) {
    log_error("Unable to parse PLY header.");
    return false;
  }

  size_t sz_rem = buffer.size();
  size_t sz_exp = num_splats * splat_size;
  if (sz_rem != sz_exp) {
    log_error(
        "Data size mismatch: %llu bytes expected but %llu bytes remaining.",
        sz_exp, sz_rem);
    return false;
  }

  for (const auto& property : layout) {
    metadata.properties.emplace(property.first, property.second.type);
  }
  metadata.num_splats = num_splats;

  return true;
}

bool SplatParserPly::parse_data(ParseSplatFn parse_splat) {
  GetPropertyFn get;
  // This pointer will be updated by calls to `get`.
  const uint8_t* splat = &buffer[0];

  switch (format) {
    case PlyFormat::ASCII: {
      log_error("ASCII format not supported.");
      return false;
    }
    case PlyFormat::BinaryBigEndian: {
      get = [this, &splat](Property property) {
        return get_property_binary<std::endian::big>(property, layout, splat);
      };
      break;
    }
    case PlyFormat::BinaryLittleEndian: {
      get = [this, &splat](Property property) {
        return get_property_binary<std::endian::little>(property, layout,
                                                        splat);
      };
      break;
    }
    default: {
      log_error("Invalid metadata format.");
      return false;
    }
  }

  for (size_t i = 0; i < num_splats; ++i) {
    parse_splat(i, get);
    splat += splat_size;
  }

  return true;
}
}  // namespace import::ply