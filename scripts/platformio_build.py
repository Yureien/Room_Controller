"""Let PlatformIO manage dependencies for both the app and bootloader builds."""
import os

Import("env")

# CMakeLists.txt sets this for the app, but PlatformIO configures the bootloader
# in a separate process that needs the same setting before CMake starts.
os.environ["IDF_COMPONENT_MANAGER"] = "0"
env["ENV"]["IDF_COMPONENT_MANAGER"] = "0"

# Generate before CMake/library dependency scanning, including on clean builds.
# Running each invocation notices every asset edit; unchanged output keeps its
# timestamp so the compiler rebuilds the page only when its contents change.
from pathlib import Path
import runpy

project = Path(env.subst("$PROJECT_DIR"))
generated = Path(env.subst("$BUILD_DIR")) / "generated"
embed_web = runpy.run_path(str(project / "scripts" / "embed_web.py"))["embed_web"]
embed_web(project / "web", generated / "web_ui.h")
env.Append(CPPPATH=[str(generated)])
