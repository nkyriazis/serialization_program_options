#include <boost/serialization/serialization.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/detail/common_iarchive.hpp>
#include <boost/program_options.hpp>
#include <tuple>
#include <iostream>
#include <list>

template<typename Tuple, typename State, typename Function>
auto fold_impl(Tuple t, State state, Function fn, std::integral_constant<int, -1>)
{
    return state;
}

template<typename Tuple, typename State, typename Function, int index>
auto fold_impl(Tuple t, State state, Function fn, std::integral_constant<int, index>)
{
    return fn(std::get<index>(t), fold_impl(t, state, fn, std::integral_constant<int, index - 1>()));
}

template<typename ...Args, typename State, typename Function>
auto fold(std::tuple<Args...> t, State state, Function fn)
{
    return fold_impl(t, state, fn, std::integral_constant<int, sizeof...(Args) - 1>());
}

// attribute wrapper
template<typename T, typename ...Attrs>
struct attributed : std::tuple<T&, Attrs...>
{
    template<typename...Args>
    attributed(Args&&...args) : std::tuple<T&, Attrs...>(std::forward<Args>(args)...) {}
    
    std::tuple<T&, Attrs...>& tuple() { return *this; }
    const std::tuple<T&, Attrs...>& tuple() const { return *this; }
};

// it's important to be const
template<typename T, typename ...Attrs>
const auto make_attributed(T& ref, Attrs&&... attrs)
{
    return attributed<T, Attrs...>(ref, std::forward<Attrs>(attrs)...);
}

// attributes
struct name { std::string value; };
struct description { std::string value; };

// the class to be reflected
struct class_with_serialize
{
    int a = 1, b = 1;
    double c = 1.0;
    
    template<typename Archive>
    void serialize(Archive &&ar, const unsigned int version)
    {
        ar
            & make_attributed(a, name{"a"})
            & make_attributed(b, name{"b"}, description{"this is not an ordinary b"})
            & make_attributed(c, description{"doubles are supported too"}, name{"c"})
            ;
    }
};

// the boost program options archive (input archive)
struct boost_options_archive : 
    public boost::archive::detail::common_iarchive<boost_options_archive>
{
    using base = boost::archive::detail::common_iarchive<boost_options_archive>;
    
    // we don't care about these but we have to define them
    void load(const boost::archive::class_id_optional_type&) {}
    void load(const boost::archive::class_id_reference_type&) {}
    void load(const boost::archive::tracking_type&) {}
    void load(const boost::archive::version_type&) {}
    void load(const boost::archive::class_name_type&) {}
    void load(const boost::archive::class_id_type&) {}
    void load(const boost::archive::object_reference_type&) {}
    void load(const boost::archive::object_id_type&) {}
                
    // this is the generic entry point
    template<typename T>
    void load_override(T& t)
    {
        this->base::load_override(t);
    }
    
    template<typename T>
    struct is_leaf
        : std::integral_constant<bool,
            std::is_fundamental<T>::value || std::is_same<T, std::string>::value>
    {};
    
    // special case for attributed primitives
    template<typename T, typename ...Attrs>
    void load_override(const attributed<T, Attrs...>& t, std::enable_if_t<is_leaf<T>::value>* = nullptr)
    {        
        std::ostringstream full;
        std::copy(std::begin(prefixes), std::end(prefixes),
            std::ostream_iterator<std::string>(full, "."));
        full << get<name>(t.tuple()).value;
        desc.add_options()
            (full.str().c_str(),
             boost::program_options::value<T>(&std::get<0>(t))->default_value(std::get<0>(t)),
             get<description>(t.tuple()).value.c_str());
    }
    
    // special case for attributed compounds
    template<typename T, typename ...Attrs>
    void load_override(const attributed<T, Attrs...>& t, std::enable_if_t<!is_leaf<T>::value>* = nullptr)
    {
        prefixes.push_back(get<name>(t.tuple()).value);
        load_override(std::get<0>(t));
        prefixes.pop_back();
    }
        
    // get from tuple by type (dirty)
    template<typename T, typename Tuple>
    static T get(Tuple &&t)
    {
        return fold(t, T(),
            [](auto t, auto state)
            {
                return std::get<std::is_same<decltype(t), T>::value ? 0 : 1>(std::tie(t, state));
            }
        );
    }
        
    boost_options_archive(const std::string &name): desc(name) {}
    
    std::list<std::string> prefixes;
    boost::program_options::options_description desc;
    
    void parse(int argc, const char **argv)
    {
        boost::program_options::variables_map vm;
        boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
        boost::program_options::notify(vm);
    }
};

int main(int argc, const char **argv)
{
    //std::cout << fold(std::make_tuple(1,2,3), 0, [](auto x, auto state) { return x + state; }) << std::endl;
    
    boost_options_archive ar("Options");
    class_with_serialize example;
    ar & make_attributed(example, name{"options"});
    ar.parse(argc, argv);
    std::cout << ar.desc << std::endl;
    
    std::cout << "a set to " << example.a << std::endl;
    std::cout << "b set to " << example.b << std::endl;
    std::cout << "c set to " << example.c << std::endl;
    
    return 0;
}
