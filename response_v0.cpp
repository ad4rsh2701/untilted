//
// Created by lure on 8/11/2025.
//

#include <chrono>
#include <cstdint>
#include <string_view>
#include <utility>
#include <new>
#include <cstddef>
#include <variant>
#include <iostream>
#include <span>

#include "../src/core/ankerl/unordered_dense.h"


enum class StatusCode : std::uint16_t {
        // SUCCESS CODES (0-99)
        OK = 0,

        // INFO CODES (100-199)
            // Nothing here, for now.

        // WARNING CODES (200-299)
        WARN_KEY_STORE_NEARING_CAPACITY = 200,  // COMMAND LEVEL

        // BUFFER RANGE (300-399)

        // CORE ERROR CODES (400-499)
        ERR_KEY_STORE_FULL = 256,               // COMMAND LEVEL
        ERR_UNKNOWN = 401,                      // COMMAND LEVEL // Impossible, but LIKELY without parser
        ERR_INVALID_ARGUMENT = 402,             // COMMAND LEVEL // Parser level gives better detail
        ERR_KEY_ALREADY_EXISTS = 403,           // COMMAND LEVEL
        ERR_KEY_NOT_FOUND = 404,                // COMMAND LEVEL // Also, mission complete: +5 xp
        ERR_VALUE_NOT_FOUND = 405,              // COMMAND LEVEL

        ERR_SOME_OPERATIONS_FAILED = 406,        // COMMAND LEVEL
        ERR_MULTIPLE_OPERATIONS_FAILED = 407,    // COMMAND LEVEL

        // GENERAL ERROR CODES (500-599)
        ERR_INVALID_KEY = 501,              // PARSER LEVEL
        ERR_INVALID_VALUE = 502,            // PARSER LEVEL
        ERR_INVALID_COMMAND = 503,          // PARSER LEVEL
        ERR_INVALID_ARGUMENT_COUNT = 504,   // PARSER LEVEL
        ERR_DOES_NOT_TAKE_ARGUMENTS = 505,  // PARSER LEVEL
        ERR_NO_ARGUMENTS_GIVEN = 506,       // PARSER LEVEL
        ERR_INVALID_DELIMITER = 507,        // PARSER LEVEL // Last fall back error code, if none of the above.
};


using RapidDataType = std::variant<std::string_view, int_fast64_t, double>;

struct RapidHash {
    using is_transparent = void;
    using is_avalanching = void; // mark class as high-quality avalanching hash

    [[nodiscard]] size_t operator()(const std::string& s) const noexcept {
        return ankerl::unordered_dense::hash<std::string>{}(s);
    }

    [[nodiscard]] size_t operator()(const std::string_view sv) const noexcept {
        return ankerl::unordered_dense::hash<std::string_view>{}(sv);
    }
};

ankerl::unordered_dense::map<
    std::string,
    RapidDataType,
    RapidHash,
    std::equal_to<>
> MemoryMap = [] {
    ankerl::unordered_dense::map<
        std::string,
        RapidDataType,
        RapidHash,
        std::equal_to<>
    > map;
    map.reserve(1000);
    // NOTE: The size is reserved to avoid rehashing during runtime.
    // This is a small size, for development purposes.
    // Adjust the size based on the expected number of entries
    // which you can set in `riri.config` (TODO: Implement this).
    return map;
}();


RapidDataType* getValue(const std::string_view key) noexcept {
    const auto it = MemoryMap.find(key);
    if (it == MemoryMap.end()) {
        return nullptr;  // key not found
    }
    return &it->second;      // key found
}



// struct RapidResponseWithValue {
//     StatusCode OverallCode;
//
//     using EntryType = std::pair<std: :string_view, std::variant<RapidDataType*, StatusCode>>;
//
//     struct ResultEntry {
//         std::string_view key;
//         StatusCode status;
//         RapidDataType* value; // nullptr if not found
//         ResultEntry(std::string_view string_view, StatusCode status_code, RapidDataType& value)
//             : key(string_view), status(status_code), value(&value) {}
//     };
//
//     alignas(EntryType)
//     std::byte ENTRY_STORE[sizeof(EntryType)*16]{};
//     alignas(ResultEntry)
//     std::byte RESULT_STORE[sizeof(ResultEntry)*16]{};
//
//     EntryType *entries = nullptr;
//     ResultEntry *results = nullptr;
//
//     std::uint8_t entry_count = 0;
//     std::uint8_t result_count = 0;
//     std::uint8_t capacity = 0;
//
//     RapidResponseWithValue() noexcept: OverallCode() {
//         entries = reinterpret_cast<EntryType *>(ENTRY_STORE);
//         results = reinterpret_cast<ResultEntry *>(RESULT_STORE);
//         capacity = 16;
//     }
//
//     void addEntry(std::string_view operation_target, std::variant<RapidDataType*, StatusCode> entry) noexcept {
//
//         // A very neat use of `new` (I was not expecting this).
//         // Basically, we are "constructing" the pair at the `failures` position (`failure_count`).
//         // Oh and yes, the memory is properly aligned and pre-allocated.
//         new(&entries[entry_count]) std::pair{operation_target, entry};
//
//         // And update the OverallCode to ERR_SOME_OPERATIONS_FAILED (406) as well.
//         OverallCode = StatusCode::ERR_SOME_OPERATIONS_FAILED;
//
//         ++entry_count;
//     }
//
//     void addResult(std::string_view operation_target, StatusCode error_code, RapidDataType& Value) noexcept {
//
//         // A very neat use of `new` (I was not expecting this).
//         // Basically, we are "constructing" the pair at the `failures` position (`failure_count`).
//         // Oh and yes, the memory is properly aligned and pre-allocated.
//         new(&results[result_count]) ResultEntry{operation_target, error_code, Value};
//
//         // And update the OverallCode to ERR_SOME_OPERATIONS_FAILED (406) as well.
//         OverallCode = StatusCode::ERR_SOME_OPERATIONS_FAILED;
//
//         ++result_count;
//     }
//
// };

struct ResponseWithVal_V1 {
    using EntryType = std::pair<std::string_view, std::variant<RapidDataType*, StatusCode>>;

    alignas(EntryType)
    std::byte STORE[sizeof(EntryType)*16]{};

    EntryType *entries = nullptr;

    std::uint8_t count = 0;
    std::uint8_t capacity = 0;

    ResponseWithVal_V1() noexcept: capacity(16) {
        entries = reinterpret_cast<EntryType *>(STORE);
    }

    void add(std::string_view operation_target, std::variant<RapidDataType*, StatusCode> entry) noexcept {
        new(&entries[count]) std::pair{operation_target, entry};
        ++count;
    }
};

struct ResponseWithVal_V2 {
    struct EntryType {
        std::string_view key;
        RapidDataType* value;
        StatusCode status;

        EntryType(const std::string_view string_view, RapidDataType * value, StatusCode status)
            : key(string_view), value(value), status(status){};
    };

    alignas(EntryType)
    std::byte STORE[sizeof(EntryType)*16]{};

    EntryType *entries = nullptr;

    std::uint8_t count = 0;
    std::uint8_t capacity = 0;

    ResponseWithVal_V2() noexcept: capacity(16) {
        entries = reinterpret_cast<EntryType *>(STORE);
    }

    void add(const std::string_view operation_target, RapidDataType* value, const StatusCode status) noexcept {
        new(&entries[count]) EntryType{operation_target, value, status};
        ++count;
    }
};

struct RapidNode {
    std::string_view key;
    RapidDataType value;

    constexpr RapidNode(const std::string_view k, RapidDataType v)
        : key(k), value(std::move(v)) {}

    constexpr explicit RapidNode(const std::string_view k)
        : key(k) {}
} typedef node, field, kv;


/*
*
*RiRiResponseContainer setCommand(std::span<RapidNode> nodes, fullResponseTag) {
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
 *
 */

ResponseWithVal_V1 GET(const std::span<RapidNode> args) {
    ResponseWithVal_V1 response;
    using enum StatusCode;

    std::uint_fast8_t failures = 0;

    for (const auto& node : args) {
        if (auto *valuePtr = getValue(node.key); valuePtr == nullptr) {
            ++failures;
            response.add(node.key, StatusCode::ERR_KEY_NOT_FOUND);
        } else {
            response.add(node.key, valuePtr);
        }
    }
    return response;
}



int main() {
    MemoryMap.insert({"hello", 123});
    MemoryMap.insert({"world", 456});
    MemoryMap.insert({"hello_2", 789});
    MemoryMap.insert({"world_3", 123});
    MemoryMap.insert({"hello_4", 456});
    MemoryMap.insert({"world_5", 789});
    MemoryMap.insert({"hello_6", 123});
    MemoryMap.insert({"world_7", 456});
    MemoryMap.insert({"hello_8", 789});
    MemoryMap.insert({"world_9", 123});
    MemoryMap.insert({"hello_10", 456});

    std::array<RapidNode, 10> searchNodes = {
        kv{"idk"},
        kv{"hello"},
        kv{"world_3"},
        kv{"hello_4"},
        kv{"world_5"},
        kv{"hello_6"},
        kv{"world_7"},
        kv{"hello_8"},
        kv{"world_9"},
        kv{"hello_10"},
    };
    std::span<RapidNode> search{searchNodes};

    using Clock = std::chrono::steady_clock;
    using duration = std::chrono::duration<double, std::micro>;
    constexpr int ITERATIONS = 100000;
    double totalFast = 0, totalFull = 0, totalFast2 = 0, totalFast3 = 0;

    for (int i = 0; i<10000; ++i) {
        auto t1 = Clock::now();
        auto r1 = GET(search);
        auto t2 = Clock::now();
        totalFast += std::chrono::duration_cast<duration>(t2 - t1).count();
    }

    std::cout << "Fast avg: " << (totalFast / ITERATIONS) << " ms\n";
}

// #include <iostream>
//
// int main() {
//
//     RapidResponseWithValue response;
//
//     RapidDataType value = 123;
//     std::string key = "hello";
//
//     const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
//
//     response.addEntry(key, &value);
//
//     const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
//     const std::chrono::duration<double, std::milli> elapsed = end - start;
//     std::cout << "\nElapsed time for response: " << elapsed.count() << " ms\n\n";
//
//     const std::chrono::steady_clock::time_point start_2 = std::chrono::steady_clock::now();
//
//     response.addResult(key, StatusCode::OK, value);
//
//     const std::chrono::steady_clock::time_point end_2 = std::chrono::steady_clock::now();
//     const std::chrono::duration<double, std::milli> elapsed_2 = end_2 - start_2;
//     std::cout << "\nElapsed time for response: " << elapsed_2.count() << " ms\n\n";
//
// }

