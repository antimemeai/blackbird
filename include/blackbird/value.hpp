#pragma once
#include "blackbird/foundation.hpp"
#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

namespace blackbird {
struct Decimal {
  bool negative = false;
  std::int64_t exponent = 0;
  std::vector<std::uint32_t> limbs; // little-endian base 10^9 coefficient
  bool operator==(const Decimal &) const = default;
};
class Number {
public:
  using Storage = std::variant<std::int64_t, std::uint64_t, double, Decimal>;
  template <std::integral T>
  explicit Number(T n)
      : data_(std::in_place_type<
                  std::conditional_t<std::is_signed_v<T>, std::int64_t, std::uint64_t>>,
              n) {}
  explicit Number(double n);
  explicit Number(Decimal n);
  explicit Number(std::string_view text);
  const Storage &storage() const { return data_; }
  std::string text() const;
  bool operator==(const Number &other) const;

private:
  Storage data_;
};
Result<Number> parse_number(std::string_view text);
bool valid_utf8(std::string_view text);
struct ValueLimits {
  std::size_t bytes = 16 * 1024 * 1024;
  std::size_t nodes = 500000;
  std::size_t depth = 128;
};
class Value {
public:
  using Array = std::vector<Value>;
  using Object = std::vector<std::pair<std::string, Value>>;
  using Storage =
      std::variant<std::nullptr_t, bool, Number, std::string, Array, Object>;
  Value() : data_(std::make_shared<Storage>(nullptr)) {}
  explicit Value(std::nullptr_t) : Value() {}
  explicit Value(bool value) : data_(std::make_shared<Storage>(value)) {}
  explicit Value(Number value) : data_(std::make_shared<Storage>(std::move(value))) {}
  explicit Value(std::string value)
      : data_(std::make_shared<Storage>(std::move(value))) {}
  explicit Value(const char *value)
      : data_(std::make_shared<Storage>(std::string{value})) {}
  explicit Value(Array value) : data_(std::make_shared<Storage>(std::move(value))) {}
  explicit Value(Object value) : data_(std::make_shared<Storage>(std::move(value))) {}
  // Mutable container borrows must remain private even if a later copy is made.
  Value(const Value &other)
      : data_(other.exposed_ ? std::make_shared<Storage>(other.value()) : other.data_) {
  }
  Value &operator=(const Value &other) {
    if (this != &other) {
      Value next{other};
      swap(next);
    }
    return *this;
  }
  Value(Value &&) noexcept = default;
  Value &operator=(Value &&) noexcept = default;
  void swap(Value &other) noexcept {
    data_.swap(other.data_);
    std::swap(exposed_, other.exposed_);
  }
  static Value object(Object value) { return Value{std::move(value)}; }
  const Storage &value() const noexcept {
    static const Storage empty{nullptr};
    return data_ ? *data_ : empty;
  }
  const std::string &string() const { return std::get<std::string>(value()); }
  const Number &number() const { return std::get<Number>(value()); }
  const Array &array() const { return std::get<Array>(value()); }
  Array &array() { return std::get<Array>(write()); }
  const Object &object() const { return std::get<Object>(value()); }
  Object &object() { return std::get<Object>(write()); }
  const Value *find(std::string_view key) const noexcept;
  // The same shared storage: equal values with identical encodings, at no cost.
  bool same(const Value &other) const noexcept { return data_ == other.data_; }
  bool operator==(const Value &other) const {
    return data_ == other.data_ || value() == other.value();
  }

private:
  Storage &write() {
    if (data_.use_count() != 1)
      data_ = std::make_shared<Storage>(value());
    exposed_ = true;
    return *data_;
  }
  std::shared_ptr<Storage> data_;
  bool exposed_ = false;
};
// Human/model presentation as Lua-shaped text, never used to transfer core values.
Result<std::string> format_value(const Value &value, ValueLimits limits = {});
} // namespace blackbird
