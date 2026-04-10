import os
import ctypes
import platform
from pathlib import Path
import logging

lib = None
devctxs = []

def init():
    global lib

    if lib is not None:
        return True

    try:
        base_path = Path(__file__).parent.resolve()
        system = platform.system()
        if system == "Windows":
            lib = ctypes.CDLL(str(base_path / "aimdo.dll"))
        elif system == "Linux":
            lib = ctypes.CDLL(str(base_path / "aimdo.so"), mode=258)
        else:
            logging.info(f"comfy-aimdo os not supported {system}")
            logging.info(f"NOTE: comfy-aimdo is currently only support for Windows and Linux")
            return False
    except Exception as e:
        logging.info(f"comfy-aimdo failed to load: {e}")
        logging.info(f"NOTE: comfy-aimdo is currently only support for Nvidia GPUs")
        return False

    lib.get_total_vram_usage.argtypes = [ctypes.c_void_p]
    lib.get_total_vram_usage.restype = ctypes.c_uint64

    lib.aimdo_analyze.argtypes = [ctypes.c_void_p]

    lib.init.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_size_t]
    lib.init.restype = ctypes.c_bool

    lib.get_devctx.argtypes = [ctypes.c_int]
    lib.get_devctx.restype = ctypes.c_void_p

    return True

def init_devices(device_ids):
    global devctxs

    if lib is None:
        return False

    requested = [int(device_id) for device_id in device_ids]
    if not requested:
        return False

    if not lib.plat_init():
        return False

    device_array = (ctypes.c_int * len(requested))(*requested)
    if lib.init(device_array, len(requested)):
        devctxs = [get_devctx(device_id) for device_id in requested]
        return True

    devctxs = []
    lib.plat_cleanup()
    return False

def init_device(device_id: int):
    return init_devices([device_id])

def get_devctx(device_id: int):
    devctx = lib.get_devctx(int(device_id))
    if devctx:
        return devctx
    raise RuntimeError(f"comfy-aimdo device {device_id} is not initialized")

def deinit():
    global lib, devctxs
    if lib is not None:
        lib.cleanup()
        devctxs = []
        lib.plat_cleanup()
    lib = None


def set_log_none(): lib.set_log_level_none()
def set_log_critical(): lib.set_log_level_critical()
def set_log_error(): lib.set_log_level_error()
def set_log_warning(): lib.set_log_level_warning()
def set_log_info(): lib.set_log_level_info()
def set_log_debug(): lib.set_log_level_debug()
def set_log_verbose(): lib.set_log_level_verbose()
def set_log_vverbose(): lib.set_log_level_vverbose()

def analyze():
    if lib is None:
        return
    for devctx in devctxs:
        lib.aimdo_analyze(devctx)

def get_total_vram_usage():
    if lib is None:
        return 0
    return sum(lib.get_total_vram_usage(devctx) for devctx in devctxs)
