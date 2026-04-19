#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

namespace http_deal {

template <typename type> struct inplace_hold {};

template <typename base_t> class derived_wrapper {
  std::unique_ptr<base_t> (*create_)();

  template <typename base, typename derived, typename t>
  using enable_if_t =
      typename std::enable_if<!std::is_same<base, derived>::value &&
                                  std::is_base_of<base, derived>::value,
                              t>::type;

public:
  template <typename derived_t, enable_if_t<base_t, derived_t, int> = 0>
  derived_wrapper(inplace_hold<derived_t>)
      : create_(&derived_wrapper::create<derived_t>) {}

  std::unique_ptr<base_t> get() { return create_(); }

  template <typename derived_t> static std::unique_ptr<base_t> create() {
    return std::make_unique<derived_t>();
  }
};

template <typename key_t, typename product_t> class factory {
  using creator_t = std::function<std::unique_ptr<product_t>()>;

  static auto container() -> auto & {
    static std::map<key_t, creator_t> map_product;
    return map_product;
  }

public:
  template <typename product_derived_t, typename... args_t>
  static void install(inplace_hold<product_derived_t> hold, args_t &&...args) {
    auto wrapper = derived_wrapper<product_t>(hold);
    container().insert_or_assign(
        key_t(std::forward<args_t>(args)...),
        [wrapper = std::move(wrapper)]() mutable { return wrapper.get(); });
  }

  template <typename creator_fn_t, typename... args_t>
  static void install_creator(creator_fn_t &&creator, args_t &&...args) {
    container().insert_or_assign(key_t(std::forward<args_t>(args)...),
                                 creator_t(std::forward<creator_fn_t>(creator)));
  }

  template <typename... args_t>
  static std::unique_ptr<product_t> get(args_t &&...args) {
    key_t key(std::forward<args_t>(args)...);
    auto it = container().find(key);
    if (container().end() != it) {
      return it->second();
    }

    if constexpr (std::is_same_v<key_t, std::string>) {
      std::string_view key_view(key);
      creator_t *best_creator = nullptr;
      size_t best_len = 0;
      for (auto &[pattern, creator] : container()) {
        if (pattern.empty() || pattern.back() != '/') {
          continue;
        }
        if (key_view.size() < pattern.size()) {
          continue;
        }
        if (key_view.substr(0, pattern.size()) != pattern) {
          continue;
        }
        if (pattern.size() > best_len) {
          best_len = pattern.size();
          best_creator = &creator;
        }
      }
      if (best_creator != nullptr) {
        return (*best_creator)();
      }
    }

    return {};
  }

  static void reset() { container().clear(); }
};

} // namespace http_deal
