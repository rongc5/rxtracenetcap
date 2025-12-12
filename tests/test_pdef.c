#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "../src/pdef/parser.h"
#include "../src/runtime/protocol.h"
#include "../src/runtime/executor.h"


#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        return false; \
    } \
} while (0)

#define TEST_PASS(msg) do { \
    printf("PASS: %s\n", msg); \
} while (0)

static const FilterRule* find_filter(const ProtocolDef* proto, const char* name) {
    if (!proto || !name) {
        return NULL;
    }
    for (uint32_t i = 0; i < proto->filter_count; i++) {
        if (strcmp(proto->filters[i].name, name) == 0) {
            return &proto->filters[i];
        }
    }
    return NULL;
}

static bool resolve_sample_path(const char* file, char* out, size_t out_size) {
    static const char* prefixes[] = {
        "",
        "tests/samples/",
        "../tests/samples/",
        "../../tests/samples/",
    };

    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        int written = snprintf(out, out_size, "%s%s", prefixes[i], file);
        if (written <= 0 || (size_t)written >= out_size) {
            continue;
        }
        if (access(out, R_OK) == 0) {
            return true;
        }
    }
    return false;
}


bool test_parse_simple(void) {
    char path[256];
    if (!resolve_sample_path("tests/samples/simple.pdef", path, sizeof(path))) {
        fprintf(stderr, "Failed to locate simple.pdef sample file\n");
        return false;
    }

    char error_msg[512];
    ProtocolDef* proto = pdef_parse_file(path, error_msg, sizeof(error_msg));

    if (!proto) {
        fprintf(stderr, "Failed to parse %s: %s\n", path, error_msg);
        return false;
    }

    TEST_ASSERT(strcmp(proto->name, "SimpleProtocol") == 0, "Protocol name mismatch");
    TEST_ASSERT(proto->port_count == 2, "Port count mismatch");
    TEST_ASSERT(proto->ports[0] == 8080, "Port 0 mismatch");
    TEST_ASSERT(proto->ports[1] == 8081, "Port 1 mismatch");
    TEST_ASSERT(proto->default_endian == ENDIAN_BIG, "Endian mismatch");
    TEST_ASSERT(proto->struct_count == 2, "Struct count mismatch");


    const StructDef* header = protocol_find_struct(proto, "Header");
    TEST_ASSERT(header != NULL, "Header struct not found");
    TEST_ASSERT(header->field_count == 4, "Header field count mismatch");
    TEST_ASSERT(header->min_size == 8, "Header size mismatch");


    const StructDef* packet = protocol_find_struct(proto, "Packet");
    TEST_ASSERT(packet != NULL, "Packet struct not found");


    uint64_t magic_val = 0;
    TEST_ASSERT(protocol_find_constant(proto, "MAGIC", &magic_val), "MAGIC constant not found");
    TEST_ASSERT(magic_val == 0x12345678, "MAGIC value mismatch");

    protocol_print(proto);
    protocol_free(proto);

    TEST_PASS("test_parse_simple");
    return true;
}


bool test_parse_game(void) {
    char path[256];
    if (!resolve_sample_path("tests/samples/game.pdef", path, sizeof(path))) {
        fprintf(stderr, "Failed to locate game.pdef sample file\n");
        return false;
    }

    char error_msg[512];
    ProtocolDef* proto = pdef_parse_file(path, error_msg, sizeof(error_msg));

    if (!proto) {
        fprintf(stderr, "Failed to parse %s: %s\n", path, error_msg);
        return false;
    }

    TEST_ASSERT(strcmp(proto->name, "MyGameProtocol") == 0, "Protocol name mismatch");
    TEST_ASSERT(proto->struct_count == 4, "Struct count mismatch");


    TEST_ASSERT(protocol_find_struct(proto, "Header") != NULL, "Header not found");
    TEST_ASSERT(protocol_find_struct(proto, "Player") != NULL, "Player not found");
    TEST_ASSERT(protocol_find_struct(proto, "Position") != NULL, "Position not found");
    TEST_ASSERT(protocol_find_struct(proto, "GamePacket") != NULL, "GamePacket not found");

    protocol_print(proto);
    protocol_free(proto);

    TEST_PASS("test_parse_game");
    return true;
}


bool test_executor_basic(void) {





    uint8_t packet[] = {
        0x12, 0x34, 0x56, 0x78,
        0x01,
        0x05,
        0x00, 0x10,
    };


    Instruction bytecode[] = {

        { OP_LOAD_U32_BE, 0, 0, 0, 0 },

        { OP_CMP_EQ, 0, 0x12345678, 0, 0 },

        { OP_JUMP_IF_FALSE, 0, 0, 0, 7 },


        { OP_LOAD_U8, 4, 0, 0, 0 },

        { OP_CMP_EQ, 0, 1, 0, 0 },

        { OP_JUMP_IF_FALSE, 0, 0, 0, 7 },


        { OP_RETURN_TRUE, 0, 0, 0, 0 },


        { OP_RETURN_FALSE, 0, 0, 0, 0 },
    };


    bool result = execute_bytecode(packet, sizeof(packet), bytecode, 8);
    TEST_ASSERT(result == true, "Matching packet should pass");


    packet[0] = 0xFF;
    result = execute_bytecode(packet, sizeof(packet), bytecode, 8);
    TEST_ASSERT(result == false, "Non-matching magic should fail");


    packet[0] = 0x12;
    packet[4] = 0x02;
    result = execute_bytecode(packet, sizeof(packet), bytecode, 8);
    TEST_ASSERT(result == false, "Non-matching version should fail");

    TEST_PASS("test_executor_basic");
    return true;
}


bool test_executor_boundary(void) {
    uint8_t packet[] = { 0x12, 0x34 };

    Instruction bytecode[] = {

        { OP_LOAD_U32_BE, 0, 0, 0, 0 },
        { OP_RETURN_TRUE, 0, 0, 0, 0 },
    };

    bool result = execute_bytecode(packet, sizeof(packet), bytecode, 2);
    TEST_ASSERT(result == false, "Packet too short should fail");

    TEST_PASS("test_executor_boundary");
    return true;
}


bool test_executor_comparisons(void) {
    uint8_t packet[] = {
        0x00, 0x00, 0x00, 0x64,
    };


    Instruction bytecode_gt[] = {
        { OP_LOAD_U32_BE, 0, 0, 0, 0 },
        { OP_CMP_GT, 0, 50, 0, 0 },
        { OP_JUMP_IF_FALSE, 0, 0, 0, 3 },
        { OP_RETURN_TRUE, 0, 0, 0, 0 },
        { OP_RETURN_FALSE, 0, 0, 0, 0 },
    };
    TEST_ASSERT(execute_bytecode(packet, 4, bytecode_gt, 5) == true, "GT: 100 > 50 should be true");


    Instruction bytecode_lt[] = {
        { OP_LOAD_U32_BE, 0, 0, 0, 0 },
        { OP_CMP_LT, 0, 200, 0, 0 },
        { OP_JUMP_IF_FALSE, 0, 0, 0, 3 },
        { OP_RETURN_TRUE, 0, 0, 0, 0 },
        { OP_RETURN_FALSE, 0, 0, 0, 0 },
    };
    TEST_ASSERT(execute_bytecode(packet, 4, bytecode_lt, 5) == true, "LT: 100 < 200 should be true");


    uint8_t packet2[] = { 0x12, 0x34, 0x56, 0x78 };
    Instruction bytecode_mask[] = {
        { OP_LOAD_U32_BE, 0, 0, 0, 0 },

        { OP_CMP_MASK, 0, 0xFF00FF00ULL, 0x12005600ULL, 0 },
        { OP_JUMP_IF_FALSE, 0, 0, 0, 3 },
        { OP_RETURN_TRUE, 0, 0, 0, 0 },
        { OP_RETURN_FALSE, 0, 0, 0, 0 },
    };
    TEST_ASSERT(execute_bytecode(packet2, 4, bytecode_mask, 5) == true, "Mask comparison should work");

    TEST_PASS("test_executor_comparisons");
    return true;
}


bool test_in_not_in_operator(void) {
    const char* src =
        "@protocol { name = \"InProto\"; endian = big; }\n"
        "Packet { uint8 type; uint8 code; }\n"
        "@filter InList { type in [1, 2, 3]; }\n"
        "@filter NotInList { code !in [0xFF, 0x10]; }\n";

    char error_msg[512] = {0};
    ProtocolDef* proto = pdef_parse_string(src, error_msg, sizeof(error_msg));
    if (!proto) {
        fprintf(stderr, "Failed to parse in/not-in pdef: %s\n", error_msg);
        return false;
    }

    const FilterRule* in_rule = find_filter(proto, "InList");
    const FilterRule* not_in_rule = find_filter(proto, "NotInList");
    TEST_ASSERT(in_rule != NULL, "InList filter not found");
    TEST_ASSERT(not_in_rule != NULL, "NotInList filter not found");

    uint8_t packet[] = { 0x02, 0x11 };
    TEST_ASSERT(execute_filter(packet, sizeof(packet), in_rule) == true,
                "type=2 should match IN list");
    packet[0] = 0x09;
    TEST_ASSERT(execute_filter(packet, sizeof(packet), in_rule) == false,
                "type=9 should not match IN list");

    uint8_t packet2[] = { 0x01, 0xFF };
    TEST_ASSERT(execute_filter(packet2, sizeof(packet2), not_in_rule) == false,
                "!in should fail when value is present");
    packet2[1] = 0x01;
    TEST_ASSERT(execute_filter(packet2, sizeof(packet2), not_in_rule) == true,
                "!in should pass when value is absent");

    protocol_free(proto);
    TEST_PASS("test_in_not_in_operator");
    return true;
}


bool test_varbytes_validation(void) {
    const char* bad_src =
        "@protocol { name = \"BadVar\"; endian = big; }\n"
        "Inner { uint8 len; varbytes payload; }\n"
        "Outer { uint8 prefix; Inner data; uint8 tail; }\n";

    char error_msg[512] = {0};
    ProtocolDef* proto = pdef_parse_string(bad_src, error_msg, sizeof(error_msg));
    TEST_ASSERT(proto == NULL, "Parser should reject nested varbytes in the middle");
    TEST_ASSERT(strstr(error_msg, "Variable-length") != NULL,
                "Error message should mention variable-length position");

    TEST_PASS("test_varbytes_validation");
    return true;
}


bool test_object_arrays(void) {
    const char* src =
        "@protocol { name = \"ArrayProto\"; endian = big; }\n"
        "Item { uint16 id; uint16 count; }\n"
        "Inventory { Item items[2]; }\n"
        "@filter FirstItem { items[0].id = 0x0010; }\n"
        "@filter SecondCount { items[1].count > 100; }\n";

    char error_msg[512] = {0};
    ProtocolDef* proto = pdef_parse_string(src, error_msg, sizeof(error_msg));
    if (!proto) {
        fprintf(stderr, "Failed to parse array proto: %s\n", error_msg);
        return false;
    }

    const StructDef* inv = protocol_find_struct(proto, "Inventory");
    TEST_ASSERT(inv != NULL, "Inventory struct not found");
    TEST_ASSERT(inv->field_count == 4, "Inventory should expand to 4 flattened fields");
    TEST_ASSERT(strcmp(inv->fields[0].name, "items[0].id") == 0, "items[0].id name mismatch");
    TEST_ASSERT(inv->fields[0].offset == 0, "items[0].id offset mismatch");
    TEST_ASSERT(strcmp(inv->fields[1].name, "items[0].count") == 0, "items[0].count name mismatch");
    TEST_ASSERT(inv->fields[1].offset == 2, "items[0].count offset mismatch");
    TEST_ASSERT(strcmp(inv->fields[2].name, "items[1].id") == 0, "items[1].id name mismatch");
    TEST_ASSERT(inv->fields[2].offset == 4, "items[1].id offset mismatch");
    TEST_ASSERT(strcmp(inv->fields[3].name, "items[1].count") == 0, "items[1].count name mismatch");
    TEST_ASSERT(inv->fields[3].offset == 6, "items[1].count offset mismatch");
    TEST_ASSERT(inv->min_size == 8, "Inventory min_size mismatch");

    const FilterRule* first_rule = find_filter(proto, "FirstItem");
    const FilterRule* second_rule = find_filter(proto, "SecondCount");
    TEST_ASSERT(first_rule != NULL, "FirstItem filter not found");
    TEST_ASSERT(second_rule != NULL, "SecondCount filter not found");

    uint8_t packet[] = {
        0x00, 0x10,
        0x00, 0x02,
        0x00, 0x01,
        0x00, 0x65,
    };

    TEST_ASSERT(execute_filter(packet, sizeof(packet), first_rule) == true,
                "items[0].id should match filter");
    packet[1] = 0x11;
    TEST_ASSERT(execute_filter(packet, sizeof(packet), first_rule) == false,
                "items[0].id mismatch should fail");

    packet[1] = 0x10;
    TEST_ASSERT(execute_filter(packet, sizeof(packet), second_rule) == true,
                "items[1].count 101 > 100 should match");
    packet[7] = 0x01;
    TEST_ASSERT(execute_filter(packet, sizeof(packet), second_rule) == false,
                "items[1].count 1 should fail");

    protocol_free(proto);
    TEST_PASS("test_object_arrays");
    return true;
}

static bool parse_custom_path(const char* path) {
    char err[512] = {0};
    ProtocolDef* proto = pdef_parse_file(path, err, sizeof(err));
    if (!proto) {
        fprintf(stderr, "Failed to parse %s: %s\n", path, err);
        return false;
    }
    printf("Parsed %s successfully. Protocol=%s, structs=%u, filters=%u\n",
           path, proto->name, proto->struct_count, proto->filter_count);
    protocol_free(proto);
    return true;
}

int main(int argc, char** argv) {
    if (argc > 1) {

        bool ok = true;
        for (int i = 1; i < argc; i++) {
            ok = ok && parse_custom_path(argv[i]);
        }
        return ok ? 0 : 1;
    }

    printf("=== PDEF Protocol Filter Test Suite ===\n\n");

    int passed = 0;
    int total = 0;

    #define RUN_TEST(test) do { \
        total++; \
        if (test()) passed++; \
        printf("\n"); \
    } while (0)

    RUN_TEST(test_parse_simple);
    RUN_TEST(test_parse_game);
    RUN_TEST(test_executor_basic);
    RUN_TEST(test_executor_boundary);
    RUN_TEST(test_executor_comparisons);
    RUN_TEST(test_in_not_in_operator);
    RUN_TEST(test_varbytes_validation);
    RUN_TEST(test_object_arrays);

    printf("=== Test Results: %d/%d passed ===\n", passed, total);

    return (passed == total) ? 0 : 1;
}
