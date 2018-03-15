#!/bin/sh

TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
SRCDIR=${SRCDIR:-$TOPDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

CHICKADEED=${CHICKADEED:-$SRCDIR/chickadeed}
CHICKADEECLI=${CHICKADEECLI:-$SRCDIR/chickadee-cli}
CHICKADEETX=${CHICKADEETX:-$SRCDIR/chickadee-tx}
CHICKADEEQT=${CHICKADEEQT:-$SRCDIR/qt/chickadee-qt}

[ ! -x $CHICKADEED ] && echo "$CHICKADEED not found or not executable." && exit 1

# The autodetected version git tag can screw up manpage output a little bit
x16rcVER=($($CHICKADEECLI --version | head -n1 | awk -F'[ -]' '{ print $6, $7 }'))

# Create a footer file with copyright content.
# This gets autodetected fine for chickadeed if --version-string is not set,
# but has different outcomes for chickadee-qt and chickadee-cli.
echo "[COPYRIGHT]" > footer.h2m
$CHICKADEED --version | sed -n '1!p' >> footer.h2m

for cmd in $CHICKADEED $CHICKADEECLI $CHICKADEETX $CHICKADEEQT; do
  cmdname="${cmd##*/}"
  help2man -N --version-string=${x16rcVER[0]} --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-${x16rcVER[1]}//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
