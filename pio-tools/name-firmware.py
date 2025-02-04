Import("env")

import os
import shutil
import pathlib
import tasmotapiolib
from os.path import join
from colorama import Fore, Back, Style


def bin_map_copy(source, target, env):
    print(Fore.GREEN + "Copying firmware.bin and firmware.map to final destination")
    print(Fore.GREEN + "Source: {}".format(source[0].path))
    print(Fore.GREEN + "Target: {}".format(target[0].path))
    print(Fore.GREEN + "Environment: {}".format(env["PIOENV"]))
    print(Fore.GREEN + "Platform: {}".format(env["PIOPLATFORM"]))

    firsttarget = pathlib.Path(target[0].path)
    print(Fore.GREEN + "First target: {}".format(firsttarget))

    # get locations and file names based on variant
    map_file = tasmotapiolib.get_final_map_path(env)
    bin_file = tasmotapiolib.get_final_bin_path(env)
    one_bin_file = bin_file
    firmware_name = env.subst("$BUILD_DIR/${PROGNAME}.bin")

    print(Fore.GREEN + "Map file: {}".format(map_file))
    print(Fore.GREEN + "Bin file: {}".format(bin_file))
    print(Fore.GREEN + "Firmware name: {}".format(firmware_name))

    if env["PIOPLATFORM"] == "espressif32":
        if "safeboot" in firmware_name:
            SAFEBOOT_SIZE = firsttarget.stat().st_size
            if SAFEBOOT_SIZE > 851967:
                print(Fore.RED + "!!! Tasmota safeboot size is too big with {} bytes. Max size is 851967 bytes !!! ".format(
                        SAFEBOOT_SIZE
                    )
                )
        if "safeboot" not in firmware_name:
            factory_tmp = pathlib.Path(firsttarget).with_suffix("")
            factory = factory_tmp.with_suffix(factory_tmp.suffix + ".factory.bin")
            one_bin_tmp = pathlib.Path(bin_file).with_suffix("")
            one_bin_file = one_bin_tmp.with_suffix(one_bin_tmp.suffix + ".factory.bin")

    print(Fore.GREEN + "map_file: {}".format(map_file))
    print(Fore.GREEN + "bin_file: {}".format(bin_file))
    print(Fore.GREEN + "one_bin_file: {}".format(one_bin_file))
    print(Fore.GREEN + "firsttarget: {}".format(firsttarget))

    # check if new target files exist and remove if necessary
    for f in [map_file, bin_file, one_bin_file]:
        if f.is_file():
            f.unlink()

    source_map_path = None
    try:
        source_map_path = tasmotapiolib.get_source_map_path(env)
    except:
        print(Fore.RED + "Could not get source map path!")

    # check if source file exists and print warning if not
    for f in [firsttarget, factory, source_map_path]:
        if not f.is_file():
            print(Fore.RED + "File does not exist: %s" % f)
        else:
            print(Fore.GREEN + "File exists: %s" % f)

    # copy firmware.bin and map to final destination
    print(Fore.GREEN + "copy firmware.bin and map to final destination")
    shutil.copy(firsttarget, bin_file)
    if env["PIOPLATFORM"] == "espressif32":
        # the map file is needed later for firmware-metrics.py
        print(Fore.GREEN + "Copy map file - skip")
        # shutil.copy(source_map_path, map_file)
        if "safeboot" not in firmware_name:
            shutil.copy(factory, one_bin_file)
    else:
        print(Fore.GREEN + "Copy map file 2")
        map_firm = join(env.subst("$BUILD_DIR")) + os.sep + "firmware.map"
        print(Fore.GREEN + "Map file: {}".format(map_firm))
        shutil.copy(source_map_path, map_firm)
        shutil.move(source_map_path, map_file)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", bin_map_copy)
