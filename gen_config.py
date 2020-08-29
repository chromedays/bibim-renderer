import subprocess, os, pprint

def get_subdirs(base_path: str):
    return [f.name for f in os.scandir(base_path) if f.is_dir()]

def main():
    pp = pprint.PrettyPrinter(width=300)

    path_table = dict()
    output = subprocess.check_output('vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath', universal_newlines=True)
    vs_install_path = output.strip()
    path_table['VSInstallPath'] = vs_install_path
    vs_version_txt_path = os.path.join(vs_install_path, 'VC\\Auxiliary\\Build\\Microsoft.VCToolsVersion.default.txt')
    vs_version = None
    with open(vs_version_txt_path) as f:
        vs_version = f.read().strip()
    
    vs_tool_path = os.path.join(vs_install_path, 'VC\\Tools\\MSVC', vs_version)
    path_table['VSBinPath_x64'] = os.path.join(vs_tool_path, 'bin\\Hostx64\\x64')
    path_table['VSLibPath_x64'] = os.path.join(vs_tool_path, 'lib\\x64')
    path_table['VSIncludePath'] = os.path.join(vs_tool_path, 'include')

    winsdk_base_path = os.path.join('C:\\Program Files (x86)\\Windows Kits\\10')
    path_table['WindowsSDKBasePath'] = winsdk_base_path
    winsdk_version = get_subdirs(os.path.join(winsdk_base_path, 'Lib'))[0]
    winsdk_lib_base_path = os.path.join(winsdk_base_path, 'Lib', winsdk_version)
    winsdk_include_base_path = os.path.join(winsdk_base_path, 'Include', winsdk_version)
    path_table['WindowsSDKLibPath_x64'] = os.path.join(winsdk_lib_base_path, 'um\\x64')
    path_table['WindowsSDKIncludePath'] = os.path.join(winsdk_include_base_path, 'um')
    path_table['WindowsSDKUcrtLibPath_x64'] = os.path.join(winsdk_lib_base_path, 'ucrt\\x64')
    path_table['WindowsSDKUcrtIncludePath'] = os.path.join(winsdk_include_base_path, 'ucrt')

    for p in path_table.values():
        if not os.path.exists(p):
            print(f'{p} doesn\'t exist')

    with open('path.gen.bff', mode='w') as f:
        f.write('#once\n')
        for k, v in sorted(path_table.items()):
            f.write(f'.{k} = \'{v}\'\n')

if __name__ == "__main__":
    main()