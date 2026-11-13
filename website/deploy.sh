#!/bin/sh
# Builds the site and puts it on pagda.org.
#   sh website/deploy.sh
# Needs ./pagda (run make first) and the aws program signed in to the account
# that holds the pagda.org bucket and its CloudFront distribution.
set -e
cd "$(dirname "$0")/.."
python3 website/build.py
aws s3 sync website/site s3://pagda.org --exclude "_home.html" --delete --only-show-errors
id=$(aws cloudfront list-distributions --query "DistributionList.Items[?Aliases.Items && contains(Aliases.Items, 'pagda.org')].Id" --output text)
aws cloudfront create-invalidation --distribution-id "$id" --paths '/*' --query Invalidation.Status --output text
echo "deployed to https://pagda.org"
