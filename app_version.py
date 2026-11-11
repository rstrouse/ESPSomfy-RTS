import os
Import("env")

# Define the folder and filename
DATA_FOLDER = "data-src"
FILENAME = "appversion"

# Construct the full path: /your/project/path/data-src/appversion
project_dir = env.get("PROJECT_DIR")
version_file_path = os.path.join(project_dir, DATA_FOLDER, FILENAME)

# Default fallback if something goes wrong
version = "0.0.0"

if os.path.exists(version_file_path):
    try:
        with open(version_file_path, "r") as f:
            version = f.read().strip()
            # Clean output for the PlatformIO console
            print(f"--- SUCCESS: Found version {version} in {version_file_path} ---")
    except Exception as e:
        print(f"--- ERROR: could not read version file: {e} ---")
else:
    print(f"--- ERROR: File NOT FOUND at {version_file_path} ---")

# Apply to the build environment
# We use escaped quotes so C++ treats it as a string literal "vX.X.X"
full_version_str = f'\\"v{version}\\"'

env.Append(CPPDEFINES=[
    ("FW_VERSION", full_version_str)
])