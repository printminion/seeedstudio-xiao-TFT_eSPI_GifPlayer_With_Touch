Import("env")

import glob
import os
from colorama import Fore, Back, Style

def FindInoNodes(env):
    src_dir = glob.escape(env.subst("$PROJECT_SRC_DIR"))
    return env.Glob(os.path.join(src_dir, "*.ino"))
#    + env.Glob(
#        os.path.join(src_dir, "tasmota_*", "*.ino")
#    )

env.AddMethod(FindInoNodes)

# Pass flashmode at build time to macro
memory_type = env.BoardConfig().get("build.arduino.memory_type", "").upper()
flash_mode = env.BoardConfig().get("build.flash_mode", "dio").upper()

print(Fore.GREEN + "name:" + env.BoardConfig().get("name"))
print(Fore.GREEN + "Memory type: " + memory_type)
print(Fore.GREEN + "Flash mode: " + flash_mode)

if "OPI_" in memory_type:
    flash_mode = "OPI"
    print(Fore.GREEN + "Overriding flash mode to: " + flash_mode)

app_flash_mode = "-DCONFIG_TASMOTA_FLASHMODE_" + flash_mode
env.Append(CXXFLAGS=[app_flash_mode])
print(Fore.GREEN + "Appended CXXFLAGS: " + app_flash_mode)
