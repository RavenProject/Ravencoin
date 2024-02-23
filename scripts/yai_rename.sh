#!/bin/bash

find -type f -not -path "./.git/*" -exec sed -i 's/bitcoin/yottaflux/g' {} +
find -type f -not -path "./.git/*" -exec sed -i 's/Bitcoin/Yottaflux/g' {} +
find -type f -not -path "./.git/*" -exec sed -i 's/BITCOIN/YOTTAFLUX/g' {} +


find -type f -not -path "./.git/*" -exec sed -i 's/Yottaflux Core developers/Bitcoin Core developers/g' {} +


# find -type f -not -path "./.git/*" -exec sed -i 's/ravencoin/yottaflux/g' {} +
# find -type f -not -path "./.git/*" -exec sed -i 's/ravencoin/yottaflux/g' {} +
# find -type f -not -path "./.git/*" -exec sed -i 's/Ravencoin/Yottaflux/g' {} +
# find -type f -not -path "./.git/*" -exec sed -i 's/RAVENCOIN/YOTTAFLUX/g' {} +
# find -type f -not -path "./.git/*" -exec sed -i 's/Yottaflux Core developers/Ravencoin Core developers/g' {} +




