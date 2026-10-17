#!/usr/bin/env bash
# Read-only preflight: reject existing tags and published OR draft releases.
set -euo pipefail
version="${1:?Usage: check-release-available.sh VERSION}"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+$ ]]; then
    echo '::error::Expected a major.minor release version.' >&2
    exit 1
fi
if git rev-parse -q --verify "refs/tags/v$version" >/dev/null; then
    echo "::error::Tag v$version already exists. Raise the version first." >&2
    exit 1
fi
tags_file="$(mktemp)"
trap 'rm -f "$tags_file"' EXIT
# Listing releases includes drafts for this authenticated repository token.
# Do not suppress API errors: failed lookup must never authorize a release.
gh api --paginate "repos/${GITHUB_REPOSITORY:?}/releases?per_page=100" \
    --jq '.[].tag_name' > "$tags_file"
if grep -Fxq -- "v$version" "$tags_file"; then
    echo "::error::Release v$version already exists (published or draft)." >&2
    exit 1
fi
