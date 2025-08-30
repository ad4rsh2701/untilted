/// THIS SHOULD BE ILLEGAL

/// POTENTIAL RESULTS: SINGLE KEY-VALUE INSERT IN 130 NANOSECONDS, WTF?!?!?


#include <cstdint>
#include <string_view>
#include <utility>
#include <new>
#include <span>
#include <variant>
#include <chrono>
#include "unordered_dense.h"

using RapidDataType = std::variant<std::string, int_fast64_t, double, bool>;

ankerl::unordered_dense::map<
    std::string,
    RapidDataType
> MemoryMap = [] {
    ankerl::unordered_dense::map<
        std::string,
        RapidDataType
    > map;
    map.reserve(200);
    // NOTE: The size is reserved to avoid rehashing during runtime.
    // This is a small size, for development purposes.
    // Adjust the size based on the expected number of entries
    // which you can set in `riri.config` (TODO: Implement this).
    return map;
}();


bool setValue(std::string&& key, const RapidDataType& value) noexcept {
    return MemoryMap.insert({std::move(key), value}).second;
}


enum class StatusCode : std::uint8_t {
    // Success codes (0-99)
    OK = 0,
    OK_WITH_ERRORS = 1,

    // Client errors (100-199)
    INVALID_ARGUMENT = 100,
    NOT_FOUND = 101,
    TYPE_ERROR = 102,
    TOO_MANY_ERRORS = 103,
    DUPLICATE_KEY = 104, // Added for handling duplicate keys

    // Server errors (200+)
    INTERNAL_ERROR = 200,
    MULTI_FAILED = 201,
};


struct RapidResponse {
    StatusCode response;

    constexpr bool ok() const noexcept {
        return response == StatusCode::OK;
    }
};

struct RapidNode {
    std::string_view key;
    RapidDataType value;

    constexpr RapidNode(const std::string_view k, RapidDataType v)
        : key(k), value(std::move(v)) {}
} typedef node, field, kv;

struct fullResponseTag {};
inline constexpr fullResponseTag FullResponseTag;

struct RiRiResponseContainer {
    static constexpr size_t INLINE_CAPACITY = 4;

    StatusCode overall_code = StatusCode::OK;

    alignas(std::pair<std::string_view, StatusCode>)
    // weird looking syntax, but not actually weird
    char inline_storage[INLINE_CAPACITY * sizeof(std::pair<std::string_view, StatusCode>)]{};

    std::pair<std::string_view, StatusCode>* failures = nullptr;
    std::uint8_t failure_count = 0;
    std::uint8_t capacity = 0;

    RiRiResponseContainer() noexcept {
        // weird looking syntax and actually weird
        failures = reinterpret_cast<std::pair<std::string_view, StatusCode>*>(inline_storage);
        capacity = INLINE_CAPACITY;
    }

    void addFailure(std::string_view key, StatusCode code) {
        if (failure_count >= capacity) {
            overall_code = StatusCode::TOO_MANY_ERRORS;
            return;
        }
        // weird looking syntax and actually not not weird?
        new (&failures[failure_count]) std::pair{key, code};
        ++failure_count;
    }

    void reset() {
        failure_count = 0;
        overall_code = StatusCode::OK;
        // No destructors needed since we only hold trivially destructible types
    }

    // for iteration
    auto begin() const noexcept { return failures; }
    auto end()   const noexcept { return failures + failure_count; }
};



RapidResponse setCommand(std::span<RapidNode> nodes) {
    using enum StatusCode;

    if (nodes.empty()) return {INVALID_ARGUMENT};

    std::uint_fast8_t failures = 0;

    for (const auto& node : nodes) {
        if (!setValue(std::string(node.key), std::move(node.value))) {
            ++failures;
        }
    }

    if (failures == 0) return {OK};
    if (failures == nodes.size()) return {MULTI_FAILED};
    return {OK_WITH_ERRORS};
}

//v2
RapidResponse setCommand2(std::span<RapidNode> nodes) {
    using enum StatusCode;

    if (nodes.empty()) {
        return {INVALID_ARGUMENT};
    }

    std::uint_fast8_t failures = 0;

    for (const auto& node : nodes) {
        if (!setValue(std::string(node.key), node.value)) {
            ++failures;
        }
    }

    const auto total = nodes.size();

    StatusCode status = OK;
    if (failures == total) {
        status = MULTI_FAILED;
    } else if (failures != 0) {
        status = OK_WITH_ERRORS;
    }

    return {status};
}

// //v3
RapidResponse setCommand3(std::span<RapidNode> nodes) {
    using enum StatusCode;

    if (nodes.empty()) {
        return {INVALID_ARGUMENT};
    }

    std::uint_fast8_t failures = 0;
    const size_t total = nodes.size();

    for (const auto& node : nodes) {
        failures += !setValue(std::string(node.key), node.value);
    }

    // Branchless status calculation
    const bool all = (failures == total);
    const bool none = (failures == 0);

    StatusCode status = 
        none ? OK :
        all  ? MULTI_FAILED :
               OK_WITH_ERRORS;

    return {status};
}


RiRiResponseContainer setCommand(std::span<RapidNode> nodes, fullResponseTag) {
    using enum StatusCode;

    RiRiResponseContainer container;

    if (nodes.empty()) {
        container.overall_code = INVALID_ARGUMENT;
        return container;
    }

    std::uint_fast8_t failures = 0;

    for (const auto& node : nodes) {
        if (!setValue(std::string(node.key), std::move(node.value))) {
            ++failures;
            container.addFailure(node.key, DUPLICATE_KEY);
        }
    }

    if (failures == 0) {
        container.overall_code = OK;
    } else if (failures == nodes.size()) {
        if (container.overall_code != TOO_MANY_ERRORS)
            container.overall_code = MULTI_FAILED;
    } else {
        if (container.overall_code != TOO_MANY_ERRORS)
            container.overall_code = OK_WITH_ERRORS;
    }

    return container;
}

constexpr std::string_view statusToString(StatusCode code) noexcept {
    using enum StatusCode;
    switch (code) {
        case OK: return "OK";
        case OK_WITH_ERRORS: return "OK_WITH_ERRORS";
        case INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case NOT_FOUND: return "NOT_FOUND";
        case TYPE_ERROR: return "TYPE_ERROR";
        case TOO_MANY_ERRORS: return "TOO_MANY_ERRORS";
        case INTERNAL_ERROR: return "INTERNAL_ERROR";
        case MULTI_FAILED: return "MULTI_FAILED";
        case DUPLICATE_KEY: return "DUPLICATE_KEY";
        default: return "UNKNOWN";

    }
}

std::string ReadResponse(const RapidResponse& res) {
    return std::string(statusToString(res.response));
}


std::string ReadResponse(const RiRiResponseContainer& container) {
    std::string result;

    result += "Overall Status: ";
    result += statusToString(container.overall_code);
    result += "\n";

    if (container.failure_count == 0) {
        return result;
    }

    result += "Failures (";
    result += std::to_string(container.failure_count);
    result += " tracked):\n";

    for (std::uint8_t i = 0; i < container.failure_count; ++i) {
        const auto& [key, code] = container.failures[i];
        result += "- ";
        result += key;
        result += " : ";
        result += statusToString(code);
        result += "\n";
    }

    if (container.overall_code == StatusCode::TOO_MANY_ERRORS) {
        result += "(Max error tracking limit reached. Additional failures not recorded.)\n";
    }

    return result;
}

#include <iostream>

int main() {
    // Example usage
    std::vector<RapidNode> nodeArray = {
        {"key1", "val1"}, {"key2", 123}, {"key3", 1.23}, {"key4", true}, {"key1", "duplicate"},
        {"key2", "val5"}, {"key3", 456}, {"key4", 7.89}, {"key5", false}, {"key6", "meh"}
    };
    std::span<RapidNode> nodes(nodeArray);


    // std::chrono::steady_clock::time_point startFull = std::chrono::steady_clock::now();
    // RiRiResponseContainer fullResponse = setCommand(nodes, FullResponseTag);
    
    // if (fullResponse.overall_code == StatusCode::OK) {
    //     std::cout<<ReadResponse(fullResponse);
    // } else {
    //     std::cout<<ReadResponse(fullResponse);
    // }
    // std::chrono::steady_clock::time_point endFull = std::chrono::steady_clock::now();
    // std::chrono::duration<double, std::milli> elapsedFull = endFull - startFull;
    // std::cout << "\nElapsed time for full response: " << elapsedFull.count() << " ms\n\n";    

    // MemoryMap.clear(); // Clear the map for the next command

    // std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    // auto response = setCommand(nodes);
    
    // if (response.ok()) {
    //     std::cout<<ReadResponse(response);
    // } else {
    //     std::cout<<ReadResponse(response);
    // }
    // std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    // std::chrono::duration<double, std::milli> elapsed = end - start;
    // std::cout << "\nElapsed time for response: " << elapsed.count() << " ms\n\n";


using Clock = std::chrono::steady_clock;
using duration = std::chrono::duration<double, std::milli>;    
constexpr int ITERATIONS = 100000;
double totalFast = 0, totalFull = 0, totalFast2 = 0, totalFast3 = 0;

for (int i = 0; i < ITERATIONS; ++i) {
    MemoryMap.clear();

    auto t1 = Clock::now();
    auto r1 = setCommand(nodes);
    auto t2 = Clock::now();
    totalFast += std::chrono::duration_cast<duration>(t2 - t1).count();

    MemoryMap.clear();

    auto t3 = Clock::now();
    auto r2 = setCommand(nodes, FullResponseTag);
    auto t4 = Clock::now();
    totalFull += std::chrono::duration_cast<duration>(t4 - t3).count();

    MemoryMap.clear(); // Clear the map for the next iteration

    auto t5 = Clock::now();
    auto r3 = setCommand2(nodes);
    auto t6 = Clock::now();
    totalFast2 += std::chrono::duration_cast<duration>(t6 - t5).count();

    MemoryMap.clear(); // Clear the map for the next iteration

    auto t7 = Clock::now();
    auto r4 = setCommand3(nodes);
    auto t8 = Clock::now();
    totalFast3 += std::chrono::duration_cast<duration>(t8 - t7).count();
}

std::cout << "Fast avg: " << (totalFast / ITERATIONS) << " ms\n";
std::cout << "Full avg: " << (totalFull / ITERATIONS) << " ms\n";
std::cout << "Fast2 avg: " << (totalFast2 / ITERATIONS) << " ms\n";
std::cout << "Fast3 avg: " << (totalFast3 / ITERATIONS) << " ms\n";

    return 0;
}