#!/usr/bin/env bash

files=()

while IFS= read -r line; do
	# Installed symlink
	if [[ "$line" =~ ^Installing[[:space:]]+symlink[[:space:]]+pointing[[:space:]]+to[[:space:]]+.+[[:space:]]+to[[:space:]]+(.+)$ ]]; then
		file="${BASH_REMATCH[1]}"

		[[ -L "$file" ]] && files+=("$file")

	# Normal installed file
	elif [[ "$line" =~ ^Installing[[:space:]]+(.+)[[:space:]]+to[[:space:]]+(.+)$ ]]; then
		from="${BASH_REMATCH[1]}"
		to="${BASH_REMATCH[2]}"

		file="$to/$(basename "$from")"

		[[ -f "$file" && ! -L "$file" ]] && files+=("$file")
	fi
done < <(meson install -C build --dry-run)

if [[ "$1" == "-y" ]]; then
	count="${#files[@]}"

	for file in "${files[@]}"; do
		sudo rm "$file"
	done

	printf '%d file(s) deleted.\n' "$count"
else
	if ((${#files[@]})); then
		printf '%s\n' "${files[@]}"
	else
		printf 'No installed files found.\n'
	fi

	printf '\nPass -y to delete them all.\n'
fi
