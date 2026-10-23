"""Compile the actual AcController with host IR/NVS/Arduino fakes."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="room-auto-cool-") as directory:
    executable = pathlib.Path(directory) / "test"
    includes = [ROOT / "test/fakes/ac", ROOT / "lib/AcController/src",
                ROOT / "lib/ControllerCore/src", ROOT / "lib/ControllerSupport/src"]
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    *["-I" + str(path) for path in includes],
                    str(ROOT / "lib/AcController/src/ac_controller.cpp"),
                    str(ROOT / "test/ac_controller_test.cpp"),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
