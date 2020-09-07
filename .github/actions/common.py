import subprocess

def run_command(cmd: list):
    cmd_str = f"'{' '.join(cmd)}'"
    print(f"Running {cmd_str}...")
    process = subprocess.run(
        cmd, shell=True, capture_output=True, universal_newlines=True
    )

    stdout = process.stdout.strip()
    stderr = process.stderr.strip()
    if len(stdout) > 0:
        for line in stdout.splitlines():
            print(f"{cmd_str} (STDOUT): {line}")
    if len(stderr) > 0:
        for line in stderr.splitlines():
            print(f"{cmd_str} (STDERR): {line}")

    if process.returncode == 0:
        print("Done.")
    else:
        raise Exception(f"{cmd_str} failed.")