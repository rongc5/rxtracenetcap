#include <stdio.h>
#include "../src/pdef/parser.h"
#include "../src/runtime/protocol.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <pdef-file>\n", argv[0]);
        return 1;
    }

    char error_msg[512];
    ProtocolDef* proto = pdef_parse_file(argv[1], error_msg, sizeof(error_msg));

    if (!proto) {
        fprintf(stderr, "Parse failed: %s\n", error_msg);
        return 1;
    }

    protocol_print(proto);
    protocol_free(proto);

    printf("\nParse successful!\n");
    return 0;
}
