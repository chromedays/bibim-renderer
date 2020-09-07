import subprocess

def main():
    print('Generating path.gen.bff...')
    subprocess.run(['dir'], shell=True)
    # subprocess.run(['python', 'gen_config.py'], shell=True, universal_newlines=True)
    print('Running FASTBuild...')
    # subprocess.run(['FBuild.exe'], shell=True, universal_newlines=True)

if __name__ == "__main__":
    main()