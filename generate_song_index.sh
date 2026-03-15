#!/bin/bash
# Scans songs/ directory and generates songs/index.json with metadata from each .ahk file.
# Run this before every git push.

SONGS_DIR="$(dirname "$0")/music"
INDEX_FILE="$SONGS_DIR/index.json"

echo "[" > "$INDEX_FILE"
first=true

for f in "$SONGS_DIR"/*.ahk; do
    [ -f "$f" ] || continue
    filename="$(basename "$f")"
    filesize=$(stat -c%s "$f" 2>/dev/null || stat -f%z "$f" 2>/dev/null)

    title=""
    author=""
    instrument=""
    part=""

    # Parse # metadata comment lines from top of file
    while IFS= read -r line; do
        trimmed="${line#"${line%%[![:space:]]*}"}"  # ltrim
        [[ "$trimmed" == \#* ]] || break
        meta="${trimmed#\#}"
        meta="${meta#"${meta%%[![:space:]]*}"}"  # ltrim after #
        case "$meta" in
            title:*)     title="${meta#title:}"; title="${title#"${title%%[![:space:]]*}"}" ;;
            author:*)    author="${meta#author:}"; author="${author#"${author%%[![:space:]]*}"}" ;;
            instrument:*) instrument="${meta#instrument:}"; instrument="${instrument#"${instrument%%[![:space:]]*}"}" ;;
            part:*)      part="${meta#part:}"; part="${part#"${part%%[![:space:]]*}"}" ;;
        esac
    done < "$f"

    # Default title from filename without extension
    if [ -z "$title" ]; then
        title="${filename%.ahk}"
        title="${title//_/ }"
    fi

    # Escape quotes for JSON
    title="${title//\"/\\\"}"
    author="${author//\"/\\\"}"
    instrument="${instrument//\"/\\\"}"
    part="${part//\"/\\\"}"

    if [ "$first" = true ]; then
        first=false
    else
        echo "," >> "$INDEX_FILE"
    fi

    printf '  {"file":"%s","title":"%s","author":"%s","instrument":"%s","part":"%s","size":%s}' \
        "$filename" "$title" "$author" "$instrument" "$part" "${filesize:-0}" >> "$INDEX_FILE"
done

echo "" >> "$INDEX_FILE"
echo "]" >> "$INDEX_FILE"

echo "Generated $INDEX_FILE"
