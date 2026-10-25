#!/bin/sh
# Non-destructive acceptance by default; controls require explicit --yes.
demo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec "$demo_root/scripts/mmctl" demo "$@"
