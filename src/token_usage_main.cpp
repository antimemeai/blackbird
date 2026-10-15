#include "blackbird/token_usage.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace {
template <class T> bool number(std::string_view text, T &value) {
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
bool rate(std::string_view text, std::optional<double> &value) {
  if (text == "-") {
    value.reset();
    return true;
  }
  double parsed = 0;
  if (!number(text, parsed) || !std::isfinite(parsed) || parsed < 0)
    return false;
  value = parsed;
  return true;
}
int usage() {
  std::fprintf(
      stderr,
      "usage: blackbird-token-usage SESSION [--max-records N] [--max-bytes N]\n"
      "       [--price DATE PROVIDER MODEL TIER INPUT OUTPUT CACHE_READ CACHE_WRITE]\n"
      "Rates are explicit USD per million tokens; '-' means absent cache rate or "
      "tier.\n"
      "Only preserved raw prefixes are read; estimates are separate from observed "
      "cost.\n");
  return 2;
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 2)
    return usage();
  blackbird::TokenUsageLimits limits;
  std::vector<blackbird::UsagePrice> prices;
  for (int position = 2; position < argc;) {
    const std::string_view option{argv[position++]};
    if (option == "--max-records" || option == "--max-bytes") {
      std::size_t count = 0;
      if (position == argc || !number(std::string_view{argv[position++]}, count) ||
          count == 0)
        return usage();
      if (option == "--max-records")
        limits.max_records = count;
      else
        limits.max_scan_bytes = count;
    } else if (option == "--price") {
      if (argc - position < 8)
        return usage();
      blackbird::UsagePrice price;
      price.date = argv[position++];
      price.provider = argv[position++];
      price.model = argv[position++];
      const std::string_view tier{argv[position++]};
      if (tier != "-")
        price.service_tier = tier;
      std::optional<double> input, output;
      if (!rate(argv[position++], input) || !rate(argv[position++], output) || !input ||
          !output || !rate(argv[position++], price.cache_read_per_million) ||
          !rate(argv[position++], price.cache_write_per_million))
        return usage();
      price.input_per_million = *input;
      price.output_per_million = *output;
      prices.push_back(std::move(price));
    } else
      return usage();
  }
  const auto report = blackbird::read_token_usage(argv[1], limits);
  if (!report.has_value()) {
    std::fprintf(stderr, "token usage: %s\n",
                 blackbird::error_name(report.error().code));
    return 1;
  }
  const auto text = blackbird::render_token_usage(report.value(), prices);
  return std::fwrite(text.data(), 1, text.size(), stdout) == text.size() ? 0 : 1;
}
