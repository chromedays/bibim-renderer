from common import run_command

def main():
    run_command(["python", "gen_config.py"])
    run_command(["FBuild.exe"])
    print('') # For some reason github action console clips last line of the output.

if __name__ == "__main__":
    main()