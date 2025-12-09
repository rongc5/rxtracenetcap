#include <stdio.h>
#include "../src/pdef/parser.h"
#include "../src/runtime/protocol.h"

int main(void) {
    char error_msg[512];
    ProtocolDef* proto = pdef_parse_file("tests/samples/game_with_filter.pdef",
                                          error_msg, sizeof(error_msg));

    if (!proto) {
        fprintf(stderr, "Parse failed: %s\n", error_msg);
        return 1;
    }

    printf("=== Filter Rules Bytecode Disassembly ===\n\n");

    for (uint32_t i = 0; i < proto->filter_count; i++) {
        filter_rule_disassemble(&proto->filters[i]);
        printf("\n");
    }

    protocol_free(proto);
    return 0;
}
