#!/bin/sh
set -e

set $(git ls-tree -r master --name-only | fgrep -v git) ip.map.{gz,bz2} *.h

echo "$@"

. ~/.ssh-agent.sh
scp -vri "$@" ubuntu@54.204.234.199:src/. &

for f in "$@"; do
	aws s3 cp "$f" s3://databus-dev/geo/$f;
done &

git push
wait
