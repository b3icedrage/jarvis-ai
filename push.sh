#!/usr/bin/env bash

set -e

# Load environment variables
if [ -f .env ]; then
    export $(grep -v '^#' .env | xargs)
else
    echo ".env file not found."
    exit 1
fi

# Verify credentials
if [ -z "$GITHUB_USERNAME" ] || [ -z "$GITHUB_TOKEN" ]; then
    echo "GITHUB_USERNAME or GITHUB_TOKEN is missing."
    exit 1
fi

# Ensure we're in a git repository
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || {
    echo "Not inside a Git repository."
    exit 1
}

# Get current remote URL
REMOTE_URL=$(git remote get-url origin)

# Convert SSH remote to HTTPS if needed
if [[ "$REMOTE_URL" =~ ^git@github.com:(.*)$ ]]; then
    REPO="${BASH_REMATCH[1]}"
    REMOTE_URL="https://github.com/$REPO"
fi

# Inject credentials into HTTPS URL
AUTH_URL=$(echo "$REMOTE_URL" | sed "s#https://#https://${GITHUB_USERNAME}:${GITHUB_TOKEN}@#")

# Push current branch
CURRENT_BRANCH=$(git branch --show-current)

git push "$AUTH_URL" "$CURRENT_BRANCH"

echo "Push completed successfully."
