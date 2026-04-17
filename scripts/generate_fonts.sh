#!/bin/bash
# Font generation script for The Six bus display
#
# Usage: ./scripts/generate_fonts.sh [large_size] [medium_size] [small_size]
# Default: large=96, medium=48, small=24
#
# After running, upload the font test demo to preview:
#   pio run -e demo-font-test -t upload

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
FONTCONVERT="$PROJECT_DIR/.pio/libdeps/demo-last-bus/LilyGo-EPD47/scripts/fontconvert.py"

# Font sizes (can be overridden via command line args)
LARGE_SIZE=${1:-96}
MEDIUM_SIZE=${2:-48}
SMALL_SIZE=${3:-18}

# System font to use (Helvetica on macOS)
FONT_FILE="/System/Library/Fonts/Helvetica.ttc"

# Check if fontconvert exists
if [ ! -f "$FONTCONVERT" ]; then
    echo "Error: fontconvert.py not found. Run 'pio run' first to download dependencies."
    exit 1
fi

# Use python3 (freetype-py should be installed)
PYTHON="python3"

echo "Generating fonts..."
echo "  Large:  ${LARGE_SIZE}pt"
echo "  Medium: ${MEDIUM_SIZE}pt"
echo "  Small:  ${SMALL_SIZE}pt"
echo ""

# Generate large font
echo "Generating FiraSansBold (${LARGE_SIZE}pt)..."
$PYTHON "$FONTCONVERT" FiraSansBold "$LARGE_SIZE" "$FONT_FILE" --compress 2>/dev/null > "$PROJECT_DIR/src/font_large.h"

# Generate medium font
echo "Generating FiraSansMedium (${MEDIUM_SIZE}pt)..."
$PYTHON "$FONTCONVERT" FiraSansMedium "$MEDIUM_SIZE" "$FONT_FILE" --compress 2>/dev/null > "$PROJECT_DIR/src/font_medium.h"

# Generate small font
echo "Generating FiraSansSmall (${SMALL_SIZE}pt)..."
$PYTHON "$FONTCONVERT" FiraSansSmall "$SMALL_SIZE" "$FONT_FILE" --compress 2>/dev/null > "$PROJECT_DIR/src/font_small.h"

echo ""
echo "Done! Fonts generated:"
echo "  src/font_large.h  (${LARGE_SIZE}pt)"
echo "  src/font_medium.h (${MEDIUM_SIZE}pt)"
echo "  src/font_small.h  (${SMALL_SIZE}pt)"
echo ""
echo "To test, run:"
echo "  pio run -e demo-font-test -t upload"
