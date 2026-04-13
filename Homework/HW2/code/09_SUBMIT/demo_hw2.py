import os
import subprocess
import pandas as pd
import csv
import argparse
import time  # Import time module

# Colors for terminal output
blue_color = "\033[94m"
green_color = "\033[92m"
red_color = "\033[91m"
yellow_color = "\033[93m"
reset_color = "\033[0m"

white_color = "\033[97m"
black_color = "\033[90m"
gray_color = "\033[37m"
purple_color = "\033[35m"
light_purple_color = "\033[35m"
sky_blue_color = "\033[96m"
cyan_color = "\033[36m"
pink_color = "\033[95m"
orange_color = "\033[38;5;214m"
bright_red_color = "\033[38;5;196m"
bright_green_color = "\033[38;5;82m"
bright_blue_color = "\033[38;5;63m"
bright_yellow_color = "\033[38;5;226m"
bright_purple_color = "\033[38;5;135m"

# Define the argument parser
def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("username", type=str, help="Username")
    parser.add_argument("Lab", type=str, choices=["hw1", "hw2", "lab1"], help="Lab")
    parser.add_argument("Submiss_Date", type=str, help="Submiss_Date")
    parser.add_argument("Submiss_Time", type=str, help="Submiss_Time")
    return parser.parse_args()

# Initialize the DataFrame for storing the result
def initialize_demo_result(args):
    demo_result = pd.DataFrame(columns=[
        "Server_Account", "Cat - signal (10%)", "Dog - signal (10%)",
        "Cat - buffer (10%)", "Dog - buffer (10%)", "Cat - fifo (10%)", "Dog - fifo (10%)", 
        "Error_Message", "Submiss_Date", "Submiss_Time", 
        "Sim_Time - signal (s)", "Sim_Time - buffer (s)", "Sim_Time - fifo (s)"
    ])
    demo_result.loc[0, "Server_Account"] = args.username
    demo_result.loc[0, "Cat - signal (10%)"] = "NaN"
    demo_result.loc[0, "Dog - signal (10%)"] = "NaN"
    demo_result.loc[0, "Cat - buffer (10%)"] = "NaN"
    demo_result.loc[0, "Dog - buffer (10%)"] = "NaN"
    demo_result.loc[0, "Cat - fifo (10%)"] = "NaN"
    demo_result.loc[0, "Dog - fifo (10%)"] = "NaN"
    demo_result.loc[0, "Error_Message"] = ""
    demo_result.loc[0, "Submiss_Date"] = args.Submiss_Date
    demo_result.loc[0, "Submiss_Time"] = args.Submiss_Time
    demo_result.loc[0, "Sim_Time - signal (s)"] = "NaN"
    demo_result.loc[0, "Sim_Time - buffer (s)"] = "NaN"
    demo_result.loc[0, "Sim_Time - fifo (s)"] = "NaN"
    return demo_result

# Run a subprocess and capture errors
def run_make(target, log_filename, directory):
    try:
        log_file_path = os.path.join(directory, log_filename)  # Ensure log files are placed inside respective directories
        with open(log_file_path, "w") as log_file:
            subprocess.run(
                ["csh", "-fc", f"source /RAID2/cad/full-custom.cshrc; make {target}"],
                cwd=directory,
                stdout=log_file, 
                stderr=subprocess.STDOUT, 
                check=True
            )
    except subprocess.CalledProcessError as e:
        print(f"{red_color}[Error] Make failed in {directory}: {e}{reset_color}")
        return False
    return True

# Read content from 'Top 100 classes:' onward in a log file
def read_from_top_classes(file_path):
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as file:
        content = file.read()
        start_index = content.find("Top 100 classes:")
        if start_index != -1:
            return content[start_index:]
        else:
            raise ValueError(f"'Top 100 classes:' not found in {file_path}")

# Compare the results of two log files starting from 'Top 100 classes:'  
def compare_files(file_a_path, file_b_path, demo_result, column_name):
    try:
        file_a_content = read_from_top_classes(file_a_path)
        file_b_content = read_from_top_classes(file_b_path)

        a_lines = file_a_content.strip().splitlines()
        b_lines = file_b_content.strip().splitlines()

        mismatch = False
        
        # We only need to check up to the length of the golden result (b_lines)
        # If student outputs more, we ignore the extra lines as long as the first len(b_lines) match.
        if len(a_lines) < len(b_lines):
            mismatch = True
            first_fail_idx = min(len(a_lines), len(b_lines) - 1)
            first_diff_a = a_lines[first_fail_idx] if first_fail_idx < len(a_lines) else "[Missing line]"
            first_diff_b = b_lines[first_fail_idx]
        else:
            first_diff_a = ""
            first_diff_b = ""
            for a_line, b_line in zip(a_lines[:len(b_lines)], b_lines):
                if a_line == b_line:
                    continue
                
                # Check for floating point tolerance in data lines
                if '|' in a_line and '|' in b_line:
                    a_parts = [p.strip() for p in a_line.split('|')]
                    b_parts = [p.strip() for p in b_line.split('|')]
                    if len(a_parts) == 4 and len(b_parts) == 4:
                        if a_parts[0] == b_parts[0] and a_parts[3] == b_parts[3]:
                            try:
                                a_val = float(a_parts[1])
                                b_val = float(b_parts[1])
                                a_pos = float(a_parts[2])
                                b_pos = float(b_parts[2])
                                # Acceptable error threshold: 0.05
                                if abs(a_val - b_val) <= 0.051 and abs(a_pos - b_pos) <= 0.051:
                                    continue
                            except ValueError:
                                pass
                
                # Real mismatch found
                mismatch = True
                first_diff_a = a_line
                first_diff_b = b_line
                break

        if not mismatch:
            if "Cat" in column_name:
                print(f"{blue_color}[Info] {file_a_path} Match Golden Result{reset_color}")
            else:
                print(f"{purple_color}[Info] {file_a_path} Match Golden Result{reset_color}")
            demo_result.loc[0, column_name] = "PASS"
        else:
            print(f"{red_color}[Error] Oops! {file_a_path} Mismatch > , <{reset_color}")
            demo_result.loc[0, column_name] = "FAIL"
            demo_result.loc[0, "Error_Message"] += f"Mismatch in {column_name}, "
            
            print(f"{blue_color}First differing line:{reset_color}")
            print(f"{red_color}File A: {first_diff_a}{reset_color}")
            print(f"{green_color}File B: {first_diff_b}{reset_color}")
            
    except ValueError as e:
        print(f"{red_color}[Error] {e}{reset_color}")
        demo_result.loc[0, column_name] = "FAIL"
        demo_result.loc[0, "Error_Message"] += f"{e}, "

# Check source code conditions for each folder
def check_source_code_conditions(directory, keyword, threshold=3):
    total_count = 0
    for file in os.listdir(directory):
        if file.endswith(".cpp") or file.endswith(".h"):
            file_path = os.path.join(directory, file)
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                total_count += content.count(keyword)
    
    if total_count < threshold:
        print(f"{red_color}[Error] No {keyword} connection in {directory}{reset_color}")
        return False
    return True

# Print the CSV result in a formatted manner
def print_csv_table(filename):
    with open(filename, 'r', newline='', encoding='utf-8', errors='ignore') as csvfile:
        csv_reader = csv.reader(csvfile)
        rows = list(csv_reader)
        cols = zip(*rows)

        print('\n')
        for col in cols:
            color = blue_color
            if col[1] in ["O", "PASS", "No_Error"]:
                color = green_color
            elif "Mismatch" in col[1] or "No" in col[1] or "not found" in col[1] or col[1] in ["X", "NaN", "FAIL"]:
                color = red_color

            print(f"{color}{col[0]}\t{col[1]}{reset_color}")
        print('\n')

def main():
    args = parse_arguments()
    demo_result = initialize_demo_result(args)
    
    print(f"{yellow_color}[Info] {args.username} SystemC start{reset_color}")

    # Run make in each directory
    hw_dirs = ["01_SOURCE/SC_SIGNAL", "01_SOURCE/SC_BUFFER", "01_SOURCE/SC_FIFO"]
    dir_names = ["signal", "buffer", "fifo"]
    columns = [
        "Cat - signal (10%)", "Dog - signal (10%)", 
        "Cat - buffer (10%)", "Dog - buffer (10%)", 
        "Cat - fifo (10%)", "Dog - fifo (10%)"
    ]
    sim_time_columns = ["Sim_Time - signal (s)", "Sim_Time - buffer (s)", "Sim_Time - fifo (s)"]

    for hw_dir, dir_name, col_cat, col_dog, sim_col, keyword in zip(hw_dirs, dir_names, columns[::2], columns[1::2], sim_time_columns, ["sc_signal", "sc_buffer", "sc_fifo"]):
        print(f"{yellow_color}[Info] Checking source code for {hw_dir}...{reset_color}")
        
        # Check source code condition before running make
        if not check_source_code_conditions(f"../{hw_dir}", keyword):
            demo_result.loc[0, col_cat] = "FAIL"
            demo_result.loc[0, col_dog] = "FAIL"
            demo_result.loc[0, "Error_Message"] += f"No {keyword} connection in {hw_dir}, "
            continue

        print(f"{yellow_color}[Info] Starting make for {hw_dir}...{reset_color}")
        
        log_cat = f"result_cat_{dir_name}.log"
        log_dog = f"result_dog_{dir_name}.log"

        print(f"{blue_color}[Info] Running make for Cat in {hw_dir}...{reset_color}")
        start_time = time.time()
        if not run_make("cat", log_cat, f"../{hw_dir}"):
            demo_result.loc[0, "Error_Message"] += f"Compile Error in {hw_dir} Cat\n"
        end_time = time.time()
        demo_result.loc[0, sim_col] = round(end_time - start_time, 2)

        # Compare log files with golden logs
        compare_files(os.path.join(f"../{hw_dir}", log_cat), "/RAID2/COURSE/2026_Spring/es26mlchip/es26mlchipTA10/hw_script_encrypt/result_cat_golden.log", demo_result, col_cat)

        print(f"{purple_color}[Info] Running make for Dog in {hw_dir}...{reset_color}")
        start_time = time.time()
        if not run_make("dog", log_dog, f"../{hw_dir}"):
            demo_result.loc[0, "Error_Message"] += f"Compile Error in {hw_dir} Dog\n"
        end_time = time.time()
        demo_result.loc[0, sim_col] = round(end_time - start_time, 2)

        # Compare log files with golden logs
        compare_files(os.path.join(f"../{hw_dir}", log_dog), "/RAID2/COURSE/2026_Spring/es26mlchip/es26mlchipTA10/hw_script_encrypt/result_dog_golden.log", demo_result, col_dog)

        # Space out the output
        print()

    # if Error_Message is empty, set the value to "No_Error"
    if demo_result.loc[0, "Error_Message"] == "":
        demo_result.loc[0, "Error_Message"] = "No_Error"
    else:
        demo_result.loc[0, "Error_Message"] = demo_result.loc[0, "Error_Message"][:-2]

    # Save the results to CSV
    demo_result.to_csv("demo_result.csv", index=False)

    # Print the formatted CSV result
    print_csv_table("demo_result.csv")

if __name__ == "__main__":
    main()