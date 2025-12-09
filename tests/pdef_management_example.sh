#!/bin/bash
BASE_URL="http://localhost:8080"

echo "==========================================="
echo " PDEF Management API 使用示例"
echo "==========================================="
echo ""

echo "步骤1: 列出所有可用的PDEF文件"
echo "-------------------------------------------"
PDEF_LIST=$(curl -s -X GET "${BASE_URL}/api/pdef/list")
echo "$PDEF_LIST" | python -m json.tool 2>/dev/null || echo "$PDEF_LIST"
echo ""
echo ""

echo "步骤2: 获取第一个PDEF文件的详细信息"
echo "-------------------------------------------"
PDEF_NAME="http.pdef"
echo "获取: $PDEF_NAME"
PDEF_CONTENT=$(curl -s -X GET "${BASE_URL}/api/pdef/get?name=${PDEF_NAME}")
echo "$PDEF_CONTENT" | python -m json.tool 2>/dev/null || echo "$PDEF_CONTENT"
echo ""
echo ""

echo "步骤3: 使用完整路径获取PDEF"
echo "-------------------------------------------"
PDEF_PATH="config/protocols/dns.pdef"
echo "获取: $PDEF_PATH"
curl -s -X GET "${BASE_URL}/api/pdef/get?path=${PDEF_PATH}" | python -m json.tool 2>/dev/null || curl -s -X GET "${BASE_URL}/api/pdef/get?path=${PDEF_PATH}"
echo ""
echo ""

echo "步骤4: 完整工作流程（上传 -> 列出 -> 获取）"
echo "-------------------------------------------"

TEST_PDEF=$(cat <<'EOF'
protocol test_example {
  port 9999;
  
  filter {
    match "TEST";
  }
}
EOF
)

echo "4.1 上传测试PDEF..."
UPLOAD_RESULT=$(curl -s -X POST "${BASE_URL}/api/pdef/upload" \
  -H "Content-Type: text/plain" \
  -d "$TEST_PDEF")
echo "$UPLOAD_RESULT" | python -m json.tool 2>/dev/null || echo "$UPLOAD_RESULT"
echo ""

UPLOADED_PATH=$(echo "$UPLOAD_RESULT" | grep -o '"/tmp/rxtracenetcap_pdef/[^"]*"' | tr -d '"')

if [ -n "$UPLOADED_PATH" ]; then
    echo "4.2 验证文件已在列表中..."
    curl -s -X GET "${BASE_URL}/api/pdef/list" | grep -q "$(basename "$UPLOADED_PATH")" && echo "? 文件在列表中" || echo "? 文件未在列表中"
    echo ""
    
    echo "4.3 读取上传的PDEF内容..."
    curl -s -X GET "${BASE_URL}/api/pdef/get?path=${UPLOADED_PATH}" | python -m json.tool 2>/dev/null || curl -s -X GET "${BASE_URL}/api/pdef/get?path=${UPLOADED_PATH}"
    echo ""
fi
echo ""

echo "==========================================="
echo " 示例完成！"
echo "==========================================="
echo ""
echo "可用的API端点："
echo "  - GET  /api/pdef/list          列出所有PDEF文件"
echo "  - GET  /api/pdef/get?name=...  按名称获取PDEF"
echo "  - GET  /api/pdef/get?path=...  按路径获取PDEF"
echo "  - POST /api/pdef/upload        上传新的PDEF"
echo ""
