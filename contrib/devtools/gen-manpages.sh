#!/bin/sh

TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
SRCDIR=${SRCDIR:-$TOPDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

RAVEND=${RAVEND:-$SRCDIR/yottafluxd}
RAVENCLI=${RAVENCLI:-$SRCDIR/yottaflux-cli}
RAVENTX=${RAVENTX:-$SRCDIR/yottaflux-tx}
RAVENQT=${RAVENQT:-$SRCDIR/qt/yottaflux-qt}

[ ! -x $RAVEND ] && echo "$RAVEND not found or not executable." && exit 1

# The autodetected version git tag can screw up manpage output a little bit
YAIVER=($($RAVENCLI --version | head -n1 | awk -F'[ -]' '{ print $6, $7 }'))

# Create a footer file with copyright content.
# This gets autodetected fine for yottafluxd if --version-string is not set,
# but has different outcomes for yottaflux-qt and yottaflux-cli.
echo "[COPYRIGHT]" > footer.h2m
$RAVEND --version | sed -n '1!p' >> footer.h2m

for cmd in $RAVEND $RAVENCLI $RAVENTX $RAVENQT; do
  cmdname="${cmd##*/}"
  help2man -N --version-string=${YAIVER[0]} --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-${YAIVER[1]}//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
