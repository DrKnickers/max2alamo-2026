#!/usr/bin/env bash
#
# Manually renew the repo's collaborators-only interaction limit.
#
# Uses your locally-authenticated `gh` CLI (no PAT required), so this is
# the no-setup option compared to the scheduled workflow at
# .github/workflows/renew-interaction-limits.yml.
#
# Run on demand or set a calendar reminder for every ~5 months.
# Idempotent: re-applying the same limit just resets the 6-month clock.

set -euo pipefail

REPO="${1:-DrKnickers/max2alamo-2026}"

echo "Renewing collaborators-only interaction limit on ${REPO}..."
echo ""
echo "Before:"
gh api "repos/${REPO}/interaction-limits" || echo "(none set)"
echo ""

gh api -X PUT \
    "repos/${REPO}/interaction-limits" \
    -f limit=collaborators_only \
    -f expiry=six_months > /dev/null

echo "After:"
gh api "repos/${REPO}/interaction-limits"
echo ""
echo "Done. The limit is good for six months from now."
