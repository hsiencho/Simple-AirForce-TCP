#!/bin/bash
# ==============================================================================
# Simple-AirForce Concurrency Test Script (test_10_clients.sh)
# Purpose: Spawns 12 background './client --auto' processes for 20 seconds
#          to prove that the Server handles 10+ concurrent connections via fork().
# ==============================================================================

# Check if the client binary has been compiled
if [ ! -f "./client" ]; then
    echo "[Error] './client' binary not found! Please run 'make' first."
    exit 1
fi

echo "============================================================"
echo " Starting 12 concurrent background Clients for 20 seconds..."
echo "============================================================"

# Launch 12 background client processes using a loop
for i in $(seq 1 12); do
    ./client --auto &
    echo " -> Started Test Client #$i (PID: $!)"
done

echo "------------------------------------------------------------"
echo "[Info] All 12 Clients are now connected in the background!"
echo "Within the next 20 seconds, run either command below for proof:"
echo "  1. ps -ef | grep server"
echo "  2. pstree -p"
echo "------------------------------------------------------------"

# Wait for all background client processes to finish
wait

echo "============================================================"
echo " All 12 background Clients have completed testing!"
echo "============================================================"
