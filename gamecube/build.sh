#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"/..
docker build gamecube -t libogc2
docker run --rm -v /etc/passwd:/etc/passwd:ro -v /etc/group:/etc/group:ro -u "$(id -u):$(id -g)" -v $PWD:$PWD -w $PWD libogc2 make dol
