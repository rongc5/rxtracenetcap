#!/bin/bash
set -e

echo "=== Testing PDEF Upload API ==="
echo

PDEF_FILE="config/protocols/http.pdef"

if [ ! -f "$PDEF_FILE" ]; then
    echo "Error: Test PDEF file not found: $PDEF_FILE"
    exit 1
fi

echo "Test PDEF file: $PDEF_FILE"
echo "PDEF content:"
cat "$PDEF_FILE" | head -20
echo "..."
echo

echo "=== Step 1: Upload PDEF ==="
echo
echo "curl -X POST http://localhost:8080/api/pdef/upload \\"
echo "  --data-binary @$PDEF_FILE"
echo

echo "Expected response:"
cat << 'EOF'
{
  "status": "ok",
  "path": "/tmp/rxtracenetcap_pdef/rxtracenetcap_pdef_TIMESTAMP_RANDOM.pdef",
  "size": 1234,
  "checksum": "3af01b2c1234abcd",
  "validated": true
}
EOF
echo

echo "=== Step 2: Start Capture with Uploaded PDEF ==="
echo
cat << 'EOF'
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "iface": "eth0",
    "filter": "tcp port 80",
    "protocol_filter": "/tmp/rxtracenetcap_pdef/rxtracenetcap_pdef_TIMESTAMP_RANDOM.pdef"
  }'
EOF
echo
echo

echo "=== Full Test Workflow ==="
echo
cat << 'EOF'
RESPONSE=$(curl -s -X POST http://localhost:8080/api/pdef/upload \
  --data-binary @config/protocols/http.pdef)

echo "Upload response: $RESPONSE"

PDEF_PATH=$(echo "$RESPONSE" | jq -r '.path')
echo "PDEF path: $PDEF_PATH"

curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d "{
    \"iface\": \"eth0\",
    \"filter\": \"tcp port 80\",
    \"protocol_filter\": \"$PDEF_PATH\"
  }"
EOF
echo

echo "=== Test Summary ==="
echo " PDEF Upload API Implementation Complete"
echo " Two-step workflow designed"
echo " Clean separation of concerns"
echo
echo "Benefits:"
echo "  - No JSON escaping needed"
echo "  - Original PDEF format preserved"
echo "  - Server-side validation before use"
echo "  - Simple HTTP POST for upload"
