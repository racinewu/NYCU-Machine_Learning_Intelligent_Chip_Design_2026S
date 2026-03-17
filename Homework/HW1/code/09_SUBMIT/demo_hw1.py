import os
import subprocess
import pandas as pd
import csv
import argparse
import sys

# Colors for terminal output
blue_color = "\033[94m"
green_color = "\033[92m"
red_color = "\033[91m"
yellow_color = "\033[93m"
reset_color = "\033[0m"

# Define the argument parser
def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("username", type=str, help="Username")
    parser.add_argument("Lab", type=str, choices=["hw1"], help="Lab")
    parser.add_argument("Submiss_Date", type=str, help="Submiss_Date")
    parser.add_argument("Submiss_Time", type=str, help="Submiss_Time")
    return parser.parse_args()

# Initialize the DataFrame for storing the result
def initialize_demo_result(args):
    demo_result = pd.DataFrame(columns=["Server_Account", "Cat (35%)", "Dog (35%)", "Error_Message", "Submiss_Date", "Submiss_Time"])
    demo_result.loc[0, "Server_Account"] = args.username
    demo_result.loc[0, "Cat (35%)"] = "X"
    demo_result.loc[0, "Dog (35%)"] = "X"
    demo_result.loc[0, "Error_Message"] = "No_Error"
    demo_result.loc[0, "Submiss_Date"] = args.Submiss_Date
    demo_result.loc[0, "Submiss_Time"] = args.Submiss_Time
    return demo_result

# Run a subprocess and capture errors
def run_make(target, log_filename):
    try:
        with open(log_filename, "w") as log_file:
            subprocess.run(
                ["csh", "-fc", f"source /RAID2/cad/full-custom.cshrc; make {target}"],
                stdout=log_file, 
                stderr=subprocess.STDOUT, 
                check=True
            )
    except subprocess.CalledProcessError as e:
        print(f"{red_color}[Error] Make failed for {target}: {e}{reset_color}")
        return False
    return True

# Read content from 'Top 100 classes:' onward in a log file
def read_from_top_classes(file_path):
    with open(file_path, 'r') as file:
        content = file.read()
        start_index = content.find("Top 100 classes:")
        if start_index != -1:
            return content[start_index:]
        else:
            raise ValueError(f"[Error] 'Top 100 classes:' not found in {file_path}")

def parse_top100_rows(content):
    rows = []
    for line in content.splitlines():
        parts = [p.strip() for p in line.split("|")]
        if len(parts) != 4 or not parts[0].isdigit():
            continue
        try:
            idx = int(parts[0])
            val = float(parts[1])
            possibility = float(parts[2])
            class_name = parts[3]
            rows.append((idx, val, possibility, class_name))
        except ValueError:
            continue
    return rows

# Compare the results of two log files starting from 'Top 100 classes:'
def compare_files(file_a_path, file_b_path, demo_result, column_name):
    try:
        file_a_content = read_from_top_classes(file_a_path)
        file_b_content = read_from_top_classes(file_b_path)
        a_rows = parse_top100_rows(file_a_content)
        b_rows = parse_top100_rows(file_b_content)

        if not a_rows or not b_rows:
            print(f"{red_color}[Error] Oops! Failed to parse Top 100 rows.{reset_color}")
            return False

        if len(a_rows) < len(b_rows):
            print(f"{red_color}[Error] Oops! {file_a_path} has fewer rows than golden result.{reset_color}")
            return False

        tol = 0.01
        for a_row, b_row in zip(a_rows, b_rows):
            idx_ok = a_row[0] == b_row[0]
            val_ok = abs(a_row[1] - b_row[1]) <= tol
            poss_ok = abs(a_row[2] - b_row[2]) <= tol
            class_ok = a_row[3] == b_row[3]
            if not (idx_ok and val_ok and poss_ok and class_ok):
                print(f"{red_color}[Error] Oops! {file_a_path} Mismatch > , <{reset_color}")
                print(f"{blue_color}First differing row (compare to 2 decimal places):{reset_color}")
                print(f"{red_color}File A: {a_row[0]:>5} | {a_row[1]:>8.2f} | {a_row[2]:>11.2f} | {a_row[3]}{reset_color}")
                print(f"{green_color}File B: {b_row[0]:>5} | {b_row[1]:>8.2f} | {b_row[2]:>11.2f} | {b_row[3]}{reset_color}")
                return False

        print(f"{blue_color}[Info] {file_a_path} Match Golden Result (2dp){reset_color}")
        demo_result.loc[0, column_name] = "O"
        return True
    except ValueError as e:
        print(f"{red_color}{e}{reset_color}")
        return False

# Print the CSV result in a formatted manner
def print_csv_table(filename):
    with open(filename, 'r', newline='') as csvfile:
        csv_reader = csv.reader(csvfile)
        rows = list(csv_reader)
        cols = zip(*rows)

        print('\n')
        for col in cols:
            color = blue_color
            if col[1] in ["O", "PASS", "No_Error"]:
                color = green_color
            elif col[1] in ["X", "NaN", "FAIL"]:
                color = red_color

            print(f"{color}{col[0]}\t{col[1]}{reset_color}")
        print('\n')

def main():
    args = parse_arguments()
    demo_result = initialize_demo_result(args)
    demo_ok = True
    
    # Change to parent directory
    os.chdir("..")
    print(f"{blue_color}[Info] {args.username} SystemC start{reset_color}")

    #Run the 'make' commands
    if not run_make("cat", "result_cat.log"):
        demo_result.loc[0, "Error_Message"] = "Compile Error"
        demo_ok = False
    if not run_make("dog", "result_dog.log"):
        demo_result.loc[0, "Error_Message"] = "Compile Error"
        demo_ok = False

    # Compare results from cat and dog logs with the golden logs
    if not compare_files('result_cat.log', 'result_cat_golden.log', demo_result, "Cat (35%)"):
        demo_ok = False
    if not compare_files('result_dog.log', 'result_dog_golden.log', demo_result, "Dog (35%)"):
        demo_ok = False

    # Save the results to CSV
    os.chdir("09_SUBMIT")
    demo_result.to_csv("demo_result.csv", index=False)

    # Print the formatted CSV result
    print_csv_table("demo_result.csv")
    return 0 

if __name__ == "__main__":
    sys.exit(main())