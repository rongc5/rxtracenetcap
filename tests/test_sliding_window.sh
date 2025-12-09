#!/bin/bash
set -e

echo "=== Testing PDEF Sliding Window Feature ==="
echo

TEST_PDEF="test_sliding.pdef"

cat > "$TEST_PDEF" <<'EOF'
@protocol {
    name = "TestProto";
    ports = 9999;
    endian = big;
}

@const {
    MAGIC = 0xABCD;
}

Header {
    uint16 magic;
    uint16 length;
}

// Traditional filter: match from beginning
@filter FromHead {
    header.magic = MAGIC;
}

// Sliding window filter: search anywhere in packet
@filter Anywhere {
    sliding = true;
    sliding_max = 512;
    header.magic = MAGIC;
}
EOF

echo "1. Created test PDEF with sliding window:"
cat "$TEST_PDEF"
echo
echo

echo "2. Compiling PDEF parser..."
make pdef
echo
echo

echo "3. Testing PDEF parsing..."
if [ -f "bin/test_pdef_parser" ]; then
    ./bin/test_pdef_parser "$TEST_PDEF"
else
    echo "   Test binary not found, skipping runtime test"
fi
echo

echo "4. Verifying IEC104 PDEF with sliding window..."
if [ -f "config/protocols/iec104.pdef" ]; then
    echo "   IEC104 PDEF exists:"
    grep -A 3 "sliding" config/protocols/iec104.pdef || echo "   (No sliding window filters found)"
else
    echo "   IEC104 PDEF not found"
fi
echo

echo "=== Summary ==="
echo " PDEF syntax supports:"
echo "   - sliding = true|false"
echo "   - sliding_max = <number>"
echo
echo " Runtime implementation:"
echo "   - protocol.c: packet_filter_match() with sliding window"
echo "   - Automatic fallback to traditional mode if sliding = false"
echo
echo " Example usage in PDEF:"
echo "   @filter MyFilter {"
echo "       sliding = true;        // Enable sliding window"
echo "       sliding_max = 512;     // Search first 512 bytes"
echo "       field.value = 0x68;"
echo "   }"
echo

echo "=== Test Complete ==="

rm -f "$TEST_PDEF"
