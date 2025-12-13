echo "==============================="
echo "HTTP SERVER TEST SUITE"
echo "==============================="

# Check if server is running
if !nc -z http://localhost:4040 2>/dev/null; then
	echo "✗ ERROR: Server is not running on port 4040"
	echo "Start server with: './server'"
	exit 1
else
	echo "✓ SUCCESS: Server is running on port 4040"
	echo ""
fi

# Get root file
echo "Get / (homepage)"
RESULT=$(curl -s http://localhost:4040/)
if echo "$RESULT" | grep -q "HELLO\|html"; then
	echo "✓ SUCCESS: Homepage returned content"
else
	echo "✗ ERROR: Homepage did not return expected content"
fi
echo ""

# Get specific file
echo "Get /index.html"
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:4040/index.html)
if [ "$HTTP_CODE" = "200" ]; then
	echo "✓ SUCCESS: Got 200 OK"
else
	echo "✗ ERROR: Got HTTP $HTTP_CODE instead of 200"
fi
echo ""

# 404 for missing file
echo "404 for missing file"
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:4040/nonexistentfile.html)
if [ "$HTTP_CODE" = "404" ]; then
	echo "✓ SUCCESS: Got 404 Not Found"
else
	echo "✗ ERROR: Expected 404 but instead got $HTTP_CODE"
fi
echo ""

# POST Request
echo "POST with body"
RESULT=$(curl -s -X POST -d "test=data" http://localhost:4040/api)
if echo "$RESULT" | grep -q "processed"; then
	echo "✓ SUCCESS: POST request processed"
else
	echo "✗ ERROR: POST request failed"
fi
echo ""

echo "==============================="
echo "TEST SUITE COMPLETE"
echo "==============================="
