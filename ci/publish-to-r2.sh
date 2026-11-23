#!/usr/bin/env bash
#
# Put one download on Cloudflare R2, with its SHA-256 beside it, and read the
# size back, so a green run means the download is the file given.
#
#   ci/publish-to-r2.sh <file> <name on R2> <content type>
#
# build-disk.yml runs it for the installer ISO and the portable file. It needs
# AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY and R2_ACCOUNT_ID, MMagTech's
# repository secrets; nothing here prints them.
#
# THREE TRIES. Since late October R2 has at times refused the last step of a
# large upload (InvalidPart). The old file stays in place when it does, and a
# plain second try has worked.

set -euo pipefail

file=${1:?usage: ci/publish-to-r2.sh <file> <name on R2> <content type>}
name=${2:?usage: ci/publish-to-r2.sh <file> <name on R2> <content type>}
type=${3:?usage: ci/publish-to-r2.sh <file> <name on R2> <content type>}
: "${R2_ACCOUNT_ID:?R2_ACCOUNT_ID is not set}"

readonly TRIES=3

export AWS_DEFAULT_REGION=auto
# Newer AWS CLIs add checksums R2 does not take; only when required.
export AWS_REQUEST_CHECKSUM_CALCULATION=when_required
export AWS_RESPONSE_CHECKSUM_VALIDATION=when_required
endpoint="https://${R2_ACCOUNT_ID}.r2.cloudflarestorage.com"

[[ -f "${file}" ]] || { echo "no file: ${file}"; exit 1; }
size=$(wc -c <"${file}" | tr -d ' ')
echo "${name}: ${file}, ${size} bytes"

sum="${RUNNER_TEMP:-/tmp}/${name}.sha256"
printf '%s  %s\n' "$(sha256sum "${file}" | cut -d' ' -f1)" "${name}" >"${sum}"
cat "${sum}"

for ((try = 1; ; try++)); do
  aws s3 cp "${file}" "s3://cabinetos/${name}" --endpoint-url "${endpoint}" \
    --content-type "${type}" --no-progress && break
  ((try < TRIES)) || { echo "${name} did not go up in ${TRIES} tries"; exit 1; }
  echo "try ${try} of ${TRIES} failed; again in 60 s"
  sleep 60
done
aws s3 cp "${sum}" "s3://cabinetos/${name}.sha256" --endpoint-url "${endpoint}" \
  --content-type text/plain --no-progress

remote=$(aws s3api head-object --bucket cabinetos --key "${name}" --endpoint-url "${endpoint}" \
  --query ContentLength --output text)
[[ "${remote}" == "${size}" ]] || { echo "size on R2 differs: ${remote}, not ${size}"; exit 1; }
echo "Published ${name}, ${remote} bytes, and its SHA-256"
