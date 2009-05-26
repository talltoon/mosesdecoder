#!/bin/sh

echo "Calling aclocal..."
aclocal
echo "Calling autoconf..."
autoconf
echo "Calling automake..."
automake

echo
echo "You should now be able to configure and build:"
echo "   ./configure --prefix=/path/to/irstlm"
echo "   make -j 4"
echo
