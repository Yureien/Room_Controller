"""Let PlatformIO manage dependencies for both the app and bootloader builds."""
import os

Import("env")

# CMakeLists.txt sets this for the app, but PlatformIO configures the bootloader
# in a separate process that needs the same setting before CMake starts.
os.environ["IDF_COMPONENT_MANAGER"] = "0"
env["ENV"]["IDF_COMPONENT_MANAGER"] = "0"
