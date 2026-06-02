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

CAD_ENV = "/RAID2/cad/full-custom.cshrc"


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("username", type=str, help="Username")
    parser.add_argument("Lab", type=str, choices=["lab3"], help="Lab")
    parser.add_argument("Submiss_Date", type=str, help="Submiss_Date")
    parser.add_argument("Submiss_Time", type=str, help="Submiss_Time")
    return parser.parse_args()


def initialize_demo_result(args):
    demo_result = pd.DataFrame(
        columns=[
            "Server_Account",
            "Compile",
            "Simulation",
            "Checker",
            "Error_Message",
            "Submiss_Date",
            "Submiss_Time",
        ]
    )

    demo_result.loc[0, "Server_Account"] = args.username
    demo_result.loc[0, "Compile"] = "X"
    demo_result.loc[0, "Simulation"] = "X"
    demo_result.loc[0, "Checker"] = "X"
    demo_result.loc[0, "Error_Message"] = "No_Error"
    demo_result.loc[0, "Submiss_Date"] = args.Submiss_Date
    demo_result.loc[0, "Submiss_Time"] = args.Submiss_Time

    return demo_result


def run_command(command, log_filename):
    """
    Run command under course CAD environment.
    This is important because ./run needs the correct libstdc++ / SystemC runtime.
    """
    wrapped_command = f"source {CAD_ENV}; {command}"

    with open(log_filename, "w") as log_file:
        result = subprocess.run(
            ["csh", "-fc", wrapped_command],
            stdout=log_file,
            stderr=subprocess.STDOUT,
        )

    if result.returncode != 0:
        print(f"{red_color}[Error] Command failed: {command}{reset_color}")
        print(f"{red_color}[Error] Return code: {result.returncode}{reset_color}")
        print(f"{red_color}[Error] Please check {log_filename}{reset_color}")
        return False

    return True


def check_checker_result(log_filename):
    """
    Check whether Lab3 simulation passes.
    """
    if not os.path.exists(log_filename):
        print(f"{red_color}[Error] {log_filename} not found.{reset_color}")
        return False

    with open(log_filename, "r", errors="ignore") as f:
        content = f.read()

    pass_msg = "Checker: All expected strings processed. Congratulations."

    fail_msgs = [
        "Checker: String INCORRECT!",
        "Checker: Received unexpected string",
    ]

    if pass_msg not in content:
        print(f"{red_color}[Error] Checker pass message not found.{reset_color}")
        return False

    for msg in fail_msgs:
        if msg in content:
            print(f"{red_color}[Error] Checker found failure message: {msg}{reset_color}")
            return False

    print(f"{green_color}[Info] Checker result PASS.{reset_color}")
    return True


def print_csv_table(filename):
    with open(filename, "r", newline="") as csvfile:
        csv_reader = csv.reader(csvfile)
        rows = list(csv_reader)
        cols = zip(*rows)

        print("\n")
        for col in cols:
            color = blue_color

            if col[1] in ["O", "PASS", "No_Error"]:
                color = green_color
            elif col[1] in ["X", "FAIL", "Compile Error", "Runtime Error", "Checker Error"]:
                color = red_color

            print(f"{color}{col[0]}\t{col[1]}{reset_color}")
        print("\n")


def save_and_print_result(demo_result):
    """
    Save result back to 09_SUBMIT/demo_result.csv.
    This script is executed from 09_SUBMIT, then cd .. to lab3 root.
    """
    os.chdir("09_SUBMIT")
    demo_result.to_csv("demo_result.csv", index=False)
    print_csv_table("demo_result.csv")


def main():
    args = parse_arguments()
    demo_result = initialize_demo_result(args)

    print(f"{blue_color}[Info] {args.username} Lab3 demo start{reset_color}")

    # 01_submit is executed inside 09_SUBMIT,
    # so go back to Lab3 root directory.
    os.chdir("..")

    # Clean old build files.
    print(f"{blue_color}[Info] Running make clean...{reset_color}")
    run_command("make clean", "result_clean.log")

    # Compile.
    print(f"{blue_color}[Info] Running make...{reset_color}")
    compile_ok = run_command("make", "result_make.log")

    if not compile_ok:
        demo_result.loc[0, "Error_Message"] = "Compile Error"
        save_and_print_result(demo_result)
        return 1

    demo_result.loc[0, "Compile"] = "O"

    # Run simulation.
    print(f"{blue_color}[Info] Running ./run...{reset_color}")
    sim_ok = run_command("./run", "result_run.log")

    # Always check result_run.log.
    # Some SystemC environments may return non-zero even when Checker output is correct.
    checker_ok = check_checker_result("result_run.log")

    if checker_ok:
        demo_result.loc[0, "Simulation"] = "O"
        demo_result.loc[0, "Checker"] = "O"
        demo_result.loc[0, "Error_Message"] = "No_Error"

        if not sim_ok:
            print(f"{yellow_color}[Warning] ./run returned non-zero, but Checker PASS was found.{reset_color}")

        save_and_print_result(demo_result)
        return 0

    if not sim_ok:
        demo_result.loc[0, "Simulation"] = "X"
        demo_result.loc[0, "Checker"] = "X"
        demo_result.loc[0, "Error_Message"] = "Runtime Error"
    else:
        demo_result.loc[0, "Simulation"] = "O"
        demo_result.loc[0, "Checker"] = "X"
        demo_result.loc[0, "Error_Message"] = "Checker Error"

    save_and_print_result(demo_result)
    return 1


if __name__ == "__main__":
    sys.exit(main())