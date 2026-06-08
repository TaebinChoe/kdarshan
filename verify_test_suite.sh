#!/bin/bash

# Clean up any previous log
rm -f test_suite_run.log test_suite.bin

# Compile test suite if needed
make kdarshan_test_suite

# Start the test program in the background
./kdarshan_test_suite &
PID=$!
echo "Started kdarshan_test_suite (PID: $PID)"

# Start kdarshan tracing this PID, enabling DXT output
sudo ./kdarshan -p $PID -d > test_suite_run.log 2>&1 &
KPID=$!
echo "Started kdarshan tracer (PID: $KPID)"

# Wait for the test suite process to complete
wait $PID
echo "kdarshan_test_suite finished."

# Let kdarshan catch the last events and stop it
sleep 1
sudo kill -INT $KPID
wait $KPID
echo "kdarshan tracer stopped."

echo "--------------------------------------------------------"
echo "Captured Log Output:"
cat test_suite_run.log
echo "--------------------------------------------------------"

# Test Assertions
exit_code=0

assert_counter() {
    local counter_name="$1"
    local expected_val="$2"
    local actual_val=$(grep -E "${counter_name}\s*:" test_suite_run.log | awk -F: '{print $2}' | xargs)
    if [ "$actual_val" = "$expected_val" ]; then
        echo -e "\e[32m✓ ${counter_name}: Expected ${expected_val}, got ${actual_val}.\e[0m"
    else
        echo -e "\e[31m✗ ${counter_name}: Expected ${expected_val}, but got '${actual_val}'.\e[0m"
        exit_code=1
    fi
}

echo "Running assertions on POSIX metrics..."
assert_counter "POSIX_OPENS" "2" # open() + dup()
assert_counter "POSIX_READS" "1"
assert_counter "POSIX_WRITES" "4"
assert_counter "POSIX_SEEKS" "2"
assert_counter "POSIX_BYTES_READ" "10"
assert_counter "POSIX_BYTES_WRITTEN" "56"
assert_counter "POSIX_MEM_NOT_ALIGNED" "1"
assert_counter "POSIX_FILE_NOT_ALIGNED" "3"
assert_counter "POSIX_SEQ_WRITES" "1"
assert_counter "POSIX_CONSEC_WRITES" "1"
assert_counter "POSIX_RW_SWITCHES" "2"
assert_counter "POSIX_FSYNCS" "1"
assert_counter "POSIX_FDSYNCS" "1"
assert_counter "POSIX_STATS" "1"
assert_counter "POSIX_DUPS" "1"
assert_counter "POSIX_MMAPS" "1"

# Verify DXT entries
dxt_w_count=$(grep -c "DXT_TRACE: WRITE" test_suite_run.log)
dxt_r_count=$(grep -c "DXT_TRACE: READ" test_suite_run.log)

if [ "$dxt_w_count" -eq 4 ]; then
    echo -e "\e[32m✓ DXT Write Traces: Expected 4, got ${dxt_w_count}.\e[0m"
else
    echo -e "\e[31m✗ DXT Write Traces: Expected 4, but got ${dxt_w_count}.\e[0m"
    exit_code=1
fi

if [ "$dxt_r_count" -eq 1 ]; then
    echo -e "\e[32m✓ DXT Read Traces: Expected 1, got ${dxt_r_count}.\e[0m"
else
    echo -e "\e[31m✗ DXT Read Traces: Expected 1, but got ${dxt_r_count}.\e[0m"
    exit_code=1
fi

if [ $exit_code -eq 0 ]; then
    echo -e "\n\e[32mALL TESTS PASSED SUCCESSFULLY!\e[0m"
else
    echo -e "\n\e[31mSOME TESTS FAILED!\e[0m"
fi

exit $exit_code
