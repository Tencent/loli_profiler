#!/bin/bash
set -e

echo "Deploying Qt dependencies for Linux..."

# Paths
RELEASE_PATH="./build/cmake/bin/release"
DEPLOY_PATH="./dist/LoliProfiler"
CLI_BINARY="$DEPLOY_PATH/LoliProfilerCLI"

if [ ! -f "$CLI_BINARY" ]; then
    echo "ERROR: LoliProfilerCLI binary not found at $CLI_BINARY"
    exit 1
fi

# Create lib directory for dependencies
mkdir -p "$DEPLOY_PATH/lib"

echo "Analyzing Qt dependencies..."

# Function to copy library and its dependencies recursively
copy_lib_with_deps() {
    local lib=$1
    local lib_name=$(basename "$lib")
    
    # Skip if already copied
    if [ -f "$DEPLOY_PATH/lib/$lib_name" ]; then
        return
    fi
    
    # Skip system libraries
    case "$lib_name" in
        libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|ld-linux*.so*)
            return
            ;;
    esac
    
    if [ -f "$lib" ]; then
        echo "Copying: $lib_name"
        cp -L "$lib" "$DEPLOY_PATH/lib/"
        
        # Get dependencies of this library
        local deps=$(ldd "$lib" 2>/dev/null | grep "=>" | awk '{print $3}' | grep -v "^$")
        for dep in $deps; do
            if [ -f "$dep" ]; then
                copy_lib_with_deps "$dep"
            fi
        done
    fi
}

# Get Qt dependencies from the binary
echo "Scanning LoliProfilerCLI for Qt dependencies..."
QT_DEPS=$(ldd "$CLI_BINARY" | grep -i qt | awk '{print $3}')

for dep in $QT_DEPS; do
    if [ -f "$dep" ]; then
        copy_lib_with_deps "$dep"
    fi
done

# Also copy other non-system dependencies
echo "Scanning for other dependencies..."
OTHER_DEPS=$(ldd "$CLI_BINARY" | grep "=>" | awk '{print $3}' | grep -v "^$")

for dep in $OTHER_DEPS; do
    if [ -f "$dep" ]; then
        dep_name=$(basename "$dep")
        # Skip system libs
        case "$dep_name" in
            libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|ld-linux*.so*|libgcc_s.so*|libstdc++.so*)
                continue
                ;;
        esac
        
        # Copy Qt and other important libs
        if [[ "$dep" == *"libQt"* ]] || [[ "$dep" == *"libicu"* ]] || [[ "$dep" == *"libpcre"* ]] || [[ "$dep" == *"libdouble-conversion"* ]]; then
            copy_lib_with_deps "$dep"
        fi
    fi
done

# Copy Qt plugins if they exist
if [ -n "$QT5Path" ] && [ -d "$QT5Path/plugins" ]; then
    echo "Copying Qt plugins..."
    mkdir -p "$DEPLOY_PATH/plugins"
    
    # Copy essential plugins
    for plugin_dir in platforms xcbglintegrations; do
        if [ -d "$QT5Path/plugins/$plugin_dir" ]; then
            echo "Copying plugin: $plugin_dir"
            cp -r "$QT5Path/plugins/$plugin_dir" "$DEPLOY_PATH/plugins/"
        fi
    done
fi

# Create qt.conf to help Qt find plugins
cat > "$DEPLOY_PATH/qt.conf" << EOF
[Paths]
Plugins = plugins
Libraries = lib
EOF

# Create launcher script that sets LD_LIBRARY_PATH
cat > "$DEPLOY_PATH/LoliProfilerCLI.sh" << 'EOF'
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$SCRIPT_DIR/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$SCRIPT_DIR/plugins"
exec "$SCRIPT_DIR/LoliProfilerCLI" "$@"
EOF

chmod +x "$DEPLOY_PATH/LoliProfilerCLI.sh"

echo "Qt deployment completed!"
echo "Use LoliProfilerCLI.sh to run the application with correct library paths."
