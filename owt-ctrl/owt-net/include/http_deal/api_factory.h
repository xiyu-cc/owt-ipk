#pragma once
#include <type_traits>
#include <memory>
#include <map>

namespace http_deal
{

template <typename type>
struct inplace_hold{};

template <typename base_t>
class derived_wrapper
{
    std::unique_ptr<base_t> (*create_)();

    template <typename base, typename derived, typename t>
    using enable_if_t = 
        typename std::enable_if<!std::is_same<base, derived>::value &&
        std::is_base_of<base, derived>::value, t>::type;

public:
    template <typename derived_t, enable_if_t<base_t, derived_t, int> = 0>
    derived_wrapper(inplace_hold<derived_t>)
        : create_(&derived_wrapper::create<derived_t>)
    {}

    std::unique_ptr<base_t> get()
    {
        return create_();
    }

    template <typename derived_t>
    static std::unique_ptr<base_t> create()
    {
        return std::make_unique<derived_t>();
    }
};

template <typename key_t, typename product_t>
class factory
{
    static auto container() -> auto&
    {
        static std::map<key_t, derived_wrapper<product_t>> map_product;
        return map_product;
    }

public:
    template <typename product_derived_t, typename... args_t>
    static void install(inplace_hold<product_derived_t> hold, args_t&&... args)
    {
        container().emplace(key_t(std::forward<args_t>(args)...), derived_wrapper<product_t>(hold));
    }

    template <typename... args_t>
    static std::unique_ptr<product_t> get(args_t&&... args)
    {
        key_t key(std::forward<args_t>(args)...);
        auto it = container().find(key);
        if (container().end() == it) return {};
        return it->second.get();
    }

    static void reset()
    {
        container().clear();
    }
};

} // namespace http_deal