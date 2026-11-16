// SOURCE OF TRUTH for the text of every error the JSON reader (config/json.hpp) can give, as the `what()` of a JsonError. Each is a
// fixed message: the reader says where in the document it was only through the caller, never in the text. Kept here, apart from the
// parser, so the wording can be read, reviewed and tested in one place.
#pragma once

namespace brunner::json_errors {
inline constexpr const char* invalid_literal = "invalid literal";
inline constexpr const char* short_escape = "short escape";
inline constexpr const char* bad_escape = "bad escape";
inline constexpr const char* expected_string = "expected string";
inline constexpr const char* unterminated_string = "unterminated string";
inline constexpr const char* control_character_in_string = "control character in string";
inline constexpr const char* invalid_utf8 = "invalid UTF-8";
inline constexpr const char* unterminated_escape = "unterminated escape";
inline constexpr const char* lone_surrogate = "lone surrogate";
inline constexpr const char* too_deep = "too deep";
inline constexpr const char* unexpected_end = "unexpected end";
inline constexpr const char* duplicate_key = "duplicate key";
inline constexpr const char* expected_colon = "expected colon";
inline constexpr const char* expected_close_brace = "expected }";
inline constexpr const char* expected_close_bracket = "expected ]";
inline constexpr const char* invalid_value = "invalid value";
inline constexpr const char* leading_zero = "leading zero";
inline constexpr const char* invalid_number = "invalid number";
inline constexpr const char* number_out_of_range = "number out of range";
inline constexpr const char* integer_out_of_range = "integer out of range";
inline constexpr const char* trailing_characters = "trailing characters";
}
