#!/bin/bash

# ==========================================
# Color Seting
# ==========================================
BLUE='\033[94m'
GREEN='\033[92m'
RED='\033[91m'
YELLOW='\033[93m'
RESET='\033[0m'

# ==========================================
# Arg
# ==========================================
USERNAME=$1
LAB=$2
SUBMISS_DATE=$3
SUBMISS_TIME=$4

# ==========================================
# Initialization
# ==========================================
DEMO_RESULT="FAIL"
ERROR_MESSAGE="No_Error"
SIM_TIME="N/A"
LOG_FILE="forwarder.log"

echo -e "${BLUE}[Info] ${USERNAME} SystemC start${RESET}"

cd ..
START_TIME=$(date +%s.%N)
# ==========================================
# make clean & make
# ==========================================
make clean > "$LOG_FILE" 2>&1
make >> "$LOG_FILE" 2>&1
MAKE_STATUS=$?

if [ $MAKE_STATUS -ne 0 ]; then
    echo -e "${RED}[Error] Compile failed.${RESET}"
    DEMO_RESULT="FAIL"
    ERROR_MESSAGE="Compile Error"
else
    ./run >> "$LOG_FILE" 2>&1
    
    # ==========================================
    # Check Result
    # ==========================================
    if grep -q "Congratulations" "$LOG_FILE"; then
        echo -e "${BLUE}[Info] Match Golden Result${RESET}"
        DEMO_RESULT="PASS"
        ERROR_MESSAGE="No_Error"
    else
        echo -e "${RED}[Error] Output mismatch or execution failed.${RESET}"
        DEMO_RESULT="FAIL"
        ERROR_MESSAGE="Output mismatch"
        # Debug 輸出
        echo -e "${YELLOW}--- Debug: Captured Output ---${RESET}"
        cat "$LOG_FILE"
        echo -e "${YELLOW}------------------------------${RESET}"
    fi
fi

END_TIME=$(date +%s.%N)
SIM_TIME=$(awk -v start="$START_TIME" -v end="$END_TIME" 'BEGIN {printf "%.2f", end - start}')

# ==========================================
# Write csv
# ==========================================
cd 09_SUBMIT || exit 1
CSV_FILE="demo_result.csv"

echo "Server_Account,Demo_Result,Error_Message,Submiss_Date,Submiss_Time,Sim_Time (s)" > "$CSV_FILE"
echo "${USERNAME},${DEMO_RESULT},${ERROR_MESSAGE},${SUBMISS_DATE},${SUBMISS_TIME},${SIM_TIME}" >> "$CSV_FILE"

echo -e "\n"
echo -e "${BLUE}Server_Account\t${USERNAME}${RESET}"
if [ "$DEMO_RESULT" == "PASS" ]; then
    echo -e "${BLUE}Demo_Result\t${GREEN}${DEMO_RESULT}${RESET}"
    echo -e "${BLUE}Error_Message\t${GREEN}${ERROR_MESSAGE}${RESET}"
else
    echo -e "${BLUE}Demo_Result\t${RED}${DEMO_RESULT}${RESET}"
    echo -e "${BLUE}Error_Message\t${RED}${ERROR_MESSAGE}${RESET}"
fi
echo -e "${BLUE}Submiss_Date\t${SUBMISS_DATE}${RESET}"
echo -e "${BLUE}Submiss_Time\t${SUBMISS_TIME}${RESET}"
echo -e "${BLUE}Sim_Time (s)\t${SIM_TIME}${RESET}"
echo -e "\n"

# ==========================================
# Return Exit Code 
# ==========================================
if [ "$DEMO_RESULT" == "FAIL" ]; then
    exit 1
else
    exit 0
fi