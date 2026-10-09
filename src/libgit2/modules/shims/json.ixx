module;
// Included here, in a module of its own that does not `import std;`, because
// <nlohmann/json.hpp> pulls in <ranges> & co. textually: in the global module
// fragment of a module that also imports std, MSVC sees those declarations
// twice and rejects them (C2382, std::ranges::_Zip_iterator_sentinel_equal).
#include <nlohmann/json.hpp>

export module nlohmann_json;

export namespace nlohmann
{
	using ::nlohmann::json;
}
