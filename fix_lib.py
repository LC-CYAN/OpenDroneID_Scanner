import os
import shutil
from os.path import join

Import("env")

def remove_linux_wifi_folder(source, target, env):
    # 定位到库文件的安装目录
    project_dir = env["PROJECT_DIR"]
    lib_deps_dir = join(project_dir, ".pio", "libdeps", env["PIOENV"])
    odid_dir = join(lib_deps_dir, "opendroneid-core-c")
    wifi_dir = join(odid_dir, "wifi")
    test_dir = join(odid_dir, "test")

    # 如果存在 wifi 文件夹，则删除
    if os.path.isdir(wifi_dir):
        print(f"[FixLib] Removing incompatible Linux folder: {wifi_dir}")
        shutil.rmtree(wifi_dir)
    
    # 如果存在 test 文件夹，也建议删除
    if os.path.isdir(test_dir):
        print(f"[FixLib] Removing test folder: {test_dir}")
        shutil.rmtree(test_dir)

# 将此操作绑定到编译预处理阶段
env.AddPreAction("buildprog", remove_linux_wifi_folder)