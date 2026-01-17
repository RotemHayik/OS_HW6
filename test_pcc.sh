#!/bin/bash

# 1. Configuration - Change these if needed
PORT=12345
IP="127.0.0.1"
TEST_FILE="test_data.txt"
SERVER_BIN="./pcc_server"
CLIENT_BIN="./pcc_client"

# 2. Cleanup - Kill any old instances that might be holding the port
echo "Cleaning up old processes..."
killall -9 pcc_server pcc_client 2>/dev/null
sleep 1

# 3. Compile - Using the required flags
echo "Compiling with required flags..."
gcc -O3 -Wall -std=c11 -D_DEFAULT_SOURCE pcc_server.c -o pcc_server
gcc -O3 -Wall -std=c11 pcc_client.c -o pcc_client

if [ $? -ne 0 ]; then
    echo "Compilation failed! Fix errors before running."
    exit 1
fi

# 4. Create Test Data - 5MB of random printable characters
echo "Generating test file..."
tr -dc ' -~' < /dev/urandom | head -c 5000000 > $TEST_FILE

# 5. Start Server - Running in background
echo "Starting server on port $PORT..."
$SERVER_BIN $PORT &
SERVER_PID=$!

# Wait for server to finish binding to the port
sleep 2

# 6. Run Clients - Sequential test
echo "Launching 5 clients..."
for i in {1..5}
do
   echo "--- Client Run #$i ---"
   $CLIENT_BIN $IP $PORT $TEST_FILE
   if [ $? -ne 0 ]; then
       echo "Client #$i failed!"
   fi
done

# 7. Shutdown and Print Stats
echo "-----------------------------------"
echo "Sending SIGINT to server (PID $SERVER_PID)..."
kill -SIGINT $SERVER_PID

# Critical: wait for server to finish printing the table
sleep 2

# 8. Final Cleanup
rm $TEST_FILE
echo "Testing complete."